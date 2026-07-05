#include "cartograph/graph_io.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cartograph {
namespace {

namespace fs = std::filesystem;

// ── on-disk format (ADR-0008) ──────────────────────────────────────────────
// A fixed header — magic, version, node count, and a table of {offset,size} for
// every section — followed by the sections themselves, each 8-byte aligned so a
// warm load can reinterpret the mapped bytes as the node columns in place. Every
// reference inside the file is a byte offset or an integer NodeId, so the layout
// is position-independent: it loads correctly at whatever address mmap returns.

constexpr char kMagic[8] = {'C', 'A', 'R', 'T', 'O', 'G', '0', '1'};
constexpr std::uint32_t kVersion = 1;

// Sections, in the order they are laid out. The node columns (KIND..HASH) and
// the arena are viewed in place; the rest are flat arrays the loader replays to
// rebuild the keyed indices.
enum Sec {
  SEC_ARENA,       // interned string bytes
  SEC_KIND,        // NodeKind[node_count]
  SEC_NAME,        // StringRef[node_count]
  SEC_FILE,        // StringRef[node_count]
  SEC_LINE,        // uint32[node_count]
  SEC_LINKAGE,     // Linkage[node_count]
  SEC_HASH,        // uint64[node_count]
  SEC_CALLS_OFF,   // uint64[node_count+1]  CSR row offsets, keyed by callee
  SEC_CALLS_TGT,   // NodeId[]              callers, concatenated per callee
  SEC_INC_OFF,     // uint64[node_count+1]  CSR row offsets, keyed by includer
  SEC_INC_TGT,     // NodeId[]              includees, concatenated per includer
  SEC_USES_OFF,    // uint64[node_count+1]  CSR row offsets, keyed by type
  SEC_USES_TGT,    // NodeId[]              users, concatenated per type
  SEC_DECLS,       // uint32[] pairs        (decl, def) declaration links
  SEC_UNRESOLVED,  // serialized unresolved includes
  SEC_DIAGNOSTICS, // serialized diagnostics
  SEC_SKIPPED,     // serialized skipped files
  SEC_COUNT,
};

struct Section {
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
};

struct Header {
  char magic[8];
  std::uint32_t version;
  std::uint32_t flags;  // reserved, 0
  std::uint64_t node_count;
  Section sections[SEC_COUNT];
};

static_assert(std::is_trivially_copyable_v<Header>);

// ── save: build the byte image, then write it ──────────────────────────────

void align8(std::string& buf) {
  while (buf.size() % 8 != 0) buf.push_back('\0');
}

// Append `bytes` of `data` as the next 8-aligned section and record its extent.
Section append(std::string& buf, const void* data, std::size_t bytes) {
  align8(buf);
  Section s{buf.size(), bytes};
  buf.append(static_cast<const char*>(data), bytes);
  return s;
}

template <class T>
Section append_span(std::string& buf, std::span<const T> v) {
  return append(buf, v.data(), v.size() * sizeof(T));
}

template <class T>
Section append_vec(std::string& buf, const std::vector<T>& v) {
  return append(buf, v.data(), v.size() * sizeof(T));
}

// Length-prefixed string into a serialized blob.
void put_str(std::string& blob, std::string_view s) {
  const std::uint32_t len = static_cast<std::uint32_t>(s.size());
  blob.append(reinterpret_cast<const char*>(&len), sizeof(len));
  blob.append(s);
}

template <class T>
void put_pod(std::string& blob, const T& v) {
  blob.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

// Build the CSR (row offsets + concatenated targets) of an adjacency indexed by
// NodeId, given a per-node accessor returning that node's target list.
template <class Fn>
void build_csr(std::size_t node_count, Fn&& row, std::vector<std::uint64_t>& off,
               std::vector<NodeId>& tgt) {
  off.assign(node_count + 1, 0);
  for (NodeId id = 0; id < node_count; ++id) {
    const std::vector<NodeId>& r = row(id);
    off[id + 1] = off[id] + r.size();
    tgt.insert(tgt.end(), r.begin(), r.end());
  }
}

}  // namespace

void save_graph(const Graph& graph, const fs::path& path) {
  const std::size_t n = graph.size();

  // Adjacency as CSR, keyed by callee / includer / type respectively.
  std::vector<std::uint64_t> calls_off, inc_off, uses_off;
  std::vector<NodeId> calls_tgt, inc_tgt, uses_tgt;
  build_csr(n, [&](NodeId id) -> const std::vector<NodeId>& {
    return graph.callers_of(id);
  }, calls_off, calls_tgt);
  build_csr(n, [&](NodeId id) -> const std::vector<NodeId>& {
    return graph.includes_of(id);
  }, inc_off, inc_tgt);
  build_csr(n, [&](NodeId id) -> const std::vector<NodeId>& {
    return graph.users_of(id);
  }, uses_off, uses_tgt);

  // Declaration links as interleaved (decl, def) pairs.
  std::vector<std::uint32_t> decls;
  for (NodeId id = 0; id < n; ++id) {
    if (const auto def = graph.definition_of(id)) {
      decls.push_back(id);
      decls.push_back(*def);
    }
  }

  // The string-bearing side tables get their own length-prefixed encoding.
  std::string unresolved;
  put_pod(unresolved, static_cast<std::uint64_t>(graph.unresolved_includes().size()));
  for (const UnresolvedInclude& u : graph.unresolved_includes()) {
    put_pod(unresolved, u.includer);
    put_pod(unresolved, static_cast<std::uint8_t>(u.is_system));
    put_pod(unresolved, u.line);
    put_str(unresolved, u.target);
  }

  std::string diagnostics;
  put_pod(diagnostics, static_cast<std::uint64_t>(graph.diagnostics().size()));
  for (const Diagnostic& d : graph.diagnostics()) {
    put_str(diagnostics, d.callee);
    put_str(diagnostics, d.caller_file);
    put_pod(diagnostics, d.caller_line);
    put_pod(diagnostics, static_cast<std::uint32_t>(d.candidates.size()));
    for (const NodeId c : d.candidates) put_pod(diagnostics, c);
  }

  std::string skipped;
  put_pod(skipped, static_cast<std::uint64_t>(graph.skipped_files().size()));
  for (const SkippedFile& s : graph.skipped_files()) {
    put_str(skipped, s.path);
    put_str(skipped, s.reason);
  }

  // Reserve the header, then append every section after it, filling the table.
  std::string buf(sizeof(Header), '\0');
  Header header{};
  std::memcpy(header.magic, kMagic, sizeof(kMagic));
  header.version = kVersion;
  header.flags = 0;
  header.node_count = n;

  const std::string_view arena = graph.arena_bytes();
  header.sections[SEC_ARENA] = append(buf, arena.data(), arena.size());
  header.sections[SEC_KIND] = append_span(buf, graph.raw_kinds());
  header.sections[SEC_NAME] = append_span(buf, graph.raw_names());
  header.sections[SEC_FILE] = append_span(buf, graph.raw_files());
  header.sections[SEC_LINE] = append_span(buf, graph.raw_lines());
  header.sections[SEC_LINKAGE] = append_span(buf, graph.raw_linkages());
  header.sections[SEC_HASH] = append_span(buf, graph.raw_hashes());
  header.sections[SEC_CALLS_OFF] = append_vec(buf, calls_off);
  header.sections[SEC_CALLS_TGT] = append_vec(buf, calls_tgt);
  header.sections[SEC_INC_OFF] = append_vec(buf, inc_off);
  header.sections[SEC_INC_TGT] = append_vec(buf, inc_tgt);
  header.sections[SEC_USES_OFF] = append_vec(buf, uses_off);
  header.sections[SEC_USES_TGT] = append_vec(buf, uses_tgt);
  header.sections[SEC_DECLS] = append_vec(buf, decls);
  header.sections[SEC_UNRESOLVED] = append(buf, unresolved.data(), unresolved.size());
  header.sections[SEC_DIAGNOSTICS] = append(buf, diagnostics.data(), diagnostics.size());
  header.sections[SEC_SKIPPED] = append(buf, skipped.data(), skipped.size());

  std::memcpy(buf.data(), &header, sizeof(Header));

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot open index file for writing: " + path.string());
  out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
  if (!out) throw std::runtime_error("failed to write index file: " + path.string());
}

// ── load: mmap the file and view / replay it ───────────────────────────────

namespace {

// An mmap'd, read-only file: opened, mapped shared-nothing (MAP_PRIVATE), and
// unmapped on destruction. A loaded Graph keeps one of these alive so the spans
// it hands out stay backed for its lifetime.
class MappedFile {
 public:
  explicit MappedFile(const fs::path& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("cannot open index file: " + path.string());
    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
      ::close(fd_);
      throw std::runtime_error("cannot stat index file: " + path.string());
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) {
      ::close(fd_);
      throw std::runtime_error("index file is empty: " + path.string());
    }
    addr_ = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (addr_ == MAP_FAILED) {
      ::close(fd_);
      throw std::runtime_error("cannot mmap index file: " + path.string());
    }
  }

  ~MappedFile() {
    if (addr_ != MAP_FAILED) ::munmap(addr_, size_);
    if (fd_ >= 0) ::close(fd_);
  }

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  std::string_view bytes() const {
    return {static_cast<const char*>(addr_), size_};
  }

 private:
  int fd_ = -1;
  void* addr_ = MAP_FAILED;
  std::size_t size_ = 0;
};

// Bounds-check a section against the mapped file and return a typed span over it.
template <class T>
std::span<const T> section_span(std::string_view bytes, const Section& s,
                                std::size_t count) {
  if (s.offset % alignof(T) != 0 || s.offset > bytes.size() ||
      s.size > bytes.size() - s.offset || s.size != count * sizeof(T)) {
    throw std::runtime_error("index file has a corrupt section");
  }
  return std::span<const T>(
      reinterpret_cast<const T*>(bytes.data() + s.offset), count);
}

// A forward cursor over a serialized side-table blob, with bounds checks.
class Reader {
 public:
  Reader(std::string_view bytes, const Section& s) {
    if (s.offset > bytes.size() || s.size > bytes.size() - s.offset) {
      throw std::runtime_error("index file has a corrupt section");
    }
    view_ = bytes.substr(s.offset, s.size);
  }

  template <class T>
  T pod() {
    T v{};
    if (pos_ + sizeof(T) > view_.size())
      throw std::runtime_error("index file truncated");
    std::memcpy(&v, view_.data() + pos_, sizeof(T));
    pos_ += sizeof(T);
    return v;
  }

  std::string str() {
    const std::uint32_t len = pod<std::uint32_t>();
    if (pos_ + len > view_.size())
      throw std::runtime_error("index file truncated");
    std::string s(view_.substr(pos_, len));
    pos_ += len;
    return s;
  }

 private:
  std::string_view view_;
  std::size_t pos_ = 0;
};

}  // namespace

Graph load_graph(const fs::path& path) {
  auto mapping = std::make_shared<MappedFile>(path);
  const std::string_view bytes = mapping->bytes();

  if (bytes.size() < sizeof(Header)) {
    throw std::runtime_error("index file is truncated: " + path.string());
  }
  Header h{};
  std::memcpy(&h, bytes.data(), sizeof(Header));
  if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0) {
    throw std::runtime_error("not a cartograph index file: " + path.string());
  }
  if (h.version != kVersion) {
    throw std::runtime_error("incompatible index version " +
                             std::to_string(h.version) + " (expected " +
                             std::to_string(kVersion) + "): " + path.string());
  }

  const std::size_t n = h.node_count;
  const Section& arena = h.sections[SEC_ARENA];
  if (arena.offset > bytes.size() || arena.size > bytes.size() - arena.offset) {
    throw std::runtime_error("index file has a corrupt section");
  }
  const std::string_view arena_bytes = bytes.substr(arena.offset, arena.size);

  const auto kinds = section_span<NodeKind>(bytes, h.sections[SEC_KIND], n);
  const auto names = section_span<StringRef>(bytes, h.sections[SEC_NAME], n);
  const auto files = section_span<StringRef>(bytes, h.sections[SEC_FILE], n);
  const auto lines = section_span<std::uint32_t>(bytes, h.sections[SEC_LINE], n);
  const auto linkages = section_span<Linkage>(bytes, h.sections[SEC_LINKAGE], n);
  const auto hashes = section_span<std::uint64_t>(bytes, h.sections[SEC_HASH], n);

  Graph graph;
  graph.adopt_mapping(mapping, StringArena::mapped(arena_bytes), kinds, names,
                      files, lines, linkages, hashes);

  // Replay the persisted edges to rebuild the keyed indices. Per-key order is
  // preserved (CSR rows are stored in order), so every adjacency query answers
  // identically to the freshly-built graph.
  const auto calls_off = section_span<std::uint64_t>(bytes, h.sections[SEC_CALLS_OFF], n + 1);
  const auto calls_tgt = section_span<NodeId>(
      bytes, h.sections[SEC_CALLS_TGT], h.sections[SEC_CALLS_TGT].size / sizeof(NodeId));
  for (NodeId callee = 0; callee < n; ++callee) {
    for (std::uint64_t k = calls_off[callee]; k < calls_off[callee + 1]; ++k) {
      graph.add_edge(calls_tgt[k], callee);
    }
  }

  const auto inc_off = section_span<std::uint64_t>(bytes, h.sections[SEC_INC_OFF], n + 1);
  const auto inc_tgt = section_span<NodeId>(
      bytes, h.sections[SEC_INC_TGT], h.sections[SEC_INC_TGT].size / sizeof(NodeId));
  for (NodeId includer = 0; includer < n; ++includer) {
    for (std::uint64_t k = inc_off[includer]; k < inc_off[includer + 1]; ++k) {
      graph.add_include(includer, inc_tgt[k]);
    }
  }

  const auto uses_off = section_span<std::uint64_t>(bytes, h.sections[SEC_USES_OFF], n + 1);
  const auto uses_tgt = section_span<NodeId>(
      bytes, h.sections[SEC_USES_TGT], h.sections[SEC_USES_TGT].size / sizeof(NodeId));
  for (NodeId type = 0; type < n; ++type) {
    for (std::uint64_t k = uses_off[type]; k < uses_off[type + 1]; ++k) {
      graph.add_uses_type(uses_tgt[k], type);
    }
  }

  const auto decls = section_span<std::uint32_t>(
      bytes, h.sections[SEC_DECLS], h.sections[SEC_DECLS].size / sizeof(std::uint32_t));
  for (std::size_t i = 0; i + 1 < decls.size(); i += 2) {
    graph.link_declaration(decls[i], decls[i + 1]);
  }

  Reader unresolved(bytes, h.sections[SEC_UNRESOLVED]);
  for (std::uint64_t count = unresolved.pod<std::uint64_t>(); count > 0; --count) {
    UnresolvedInclude u;
    u.includer = unresolved.pod<NodeId>();
    u.is_system = unresolved.pod<std::uint8_t>() != 0;
    u.line = unresolved.pod<std::uint32_t>();
    u.target = unresolved.str();
    graph.add_unresolved_include(std::move(u));
  }

  Reader diagnostics(bytes, h.sections[SEC_DIAGNOSTICS]);
  for (std::uint64_t count = diagnostics.pod<std::uint64_t>(); count > 0; --count) {
    Diagnostic d;
    d.callee = diagnostics.str();
    d.caller_file = diagnostics.str();
    d.caller_line = diagnostics.pod<std::uint32_t>();
    const std::uint32_t cands = diagnostics.pod<std::uint32_t>();
    d.candidates.reserve(cands);
    for (std::uint32_t j = 0; j < cands; ++j) d.candidates.push_back(diagnostics.pod<NodeId>());
    graph.add_diagnostic(std::move(d));
  }

  Reader skipped(bytes, h.sections[SEC_SKIPPED]);
  for (std::uint64_t count = skipped.pod<std::uint64_t>(); count > 0; --count) {
    SkippedFile s;
    s.path = skipped.str();
    s.reason = skipped.str();
    graph.add_skipped_file(std::move(s));
  }

  return graph;
}

}  // namespace cartograph
