
#include "cartograph/indexer.hpp"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cartograph/extractor.hpp"
#include "cartograph/parallel.hpp"
#include "cartograph/parser.hpp"

namespace cartograph {
namespace {

// True for the C source and header extensions this slice indexes.
bool is_c_source(const std::filesystem::path& path) {
  const std::filesystem::path ext = path.extension();
  return ext == ".c" || ext == ".h";
}

// Read the whole file into `out`. Returns false if it cannot be opened.
bool read_file(const std::filesystem::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

// 64-bit FNV-1a content hash. Deterministic and dependency-free — the property
// that matters for the incremental-reindex groundwork (issue 0016), where a
// file's stored hash is compared against a fresh one to decide if it changed.
std::uint64_t content_hash(std::string_view data) {
  std::uint64_t hash = 1469598103934665603ULL;  // FNV offset basis
  for (const unsigned char byte : data) {
    hash ^= byte;
    hash *= 1099511628211ULL;  // FNV prime
  }
  return hash;
}

// Parse one already-read file's source into its local facts (Phase A). Facts
// carry names and source locations only — never NodeIDs — so nothing is stitched
// into the graph until the sequential merge (Phase B). A malformed file still
// yields a (partial) tree from the error-tolerant parser; rather than index
// half-parsed garbage we flag it SyntaxError and let the merge record the skip,
// so a single bad file doesn't poison the whole index.
void parse_source(ParsedFile& pf, const std::string& source, Parser& parser) {
  const Tree tree = parser.parse(source);
  if (tree.has_error()) {
    pf.status = ParsedFile::Status::SyntaxError;
    return;
  }
  pf.status = ParsedFile::Status::Ok;
  pf.hash = content_hash(source);
  pf.includes = extract_includes(tree, source);
  pf.definitions = extract_definitions(tree, source);
  pf.calls = extract_calls(tree, source);
  pf.declarations = extract_declarations(tree, source);
  pf.types = extract_types(tree, source);
  pf.type_uses = extract_type_uses(tree, source);
}

// Phase A — parse. For each path, read the file and either reuse the facts of an
// unchanged file or parse it fresh, in parallel across `workers`. `prior_by_path`
// maps a file's path to its artifacts from a previous run; a hit whose stored
// hash matches the freshly read content is reused (skipping the expensive
// tree-sitter parse + extraction) and marked in `reused[i]` — the incremental
// win of issue 0016. An empty map degrades to a full from-scratch parse. The
// crawl order in `paths` fixes each file's slot, so the merge that follows is
// deterministic regardless of which worker finished first, and each worker owns
// a thread-local Parser writing only its own `parsed[i]` — share-nothing.
std::vector<ParsedFile> parse_files(
    const std::vector<std::filesystem::path>& paths,
    const std::unordered_map<std::string, const ParsedFile*>& prior_by_path,
    unsigned workers, std::vector<char>& reused) {
  std::vector<ParsedFile> parsed(paths.size());
  reused.assign(paths.size(), 0);
  parallel_for(paths.size(), workers, [&](std::size_t i) {
    ParsedFile& pf = parsed[i];
    pf.path = paths[i];

    std::string source;
    if (!read_file(pf.path, source)) {
      pf.status = ParsedFile::Status::Unreadable;
      return;
    }

    // Reuse the prior artifacts when the content hash proves the file is
    // unchanged: the hash is cheap, the parse it saves is not.
    const std::uint64_t hash = content_hash(source);
    const auto found = prior_by_path.find(pf.path.string());
    if (found != prior_by_path.end() &&
        found->second->status == ParsedFile::Status::Ok &&
        found->second->hash == hash) {
      pf = *found->second;
      reused[i] = 1;
      return;
    }

    thread_local Parser parser;
    parse_source(pf, source, parser);
  });
  return parsed;
}

// Crawl the C sources under `dir` in a single sequential walk. The error-code
// overloads plus skip_permission_denied keep the crawl from throwing on an
// unreadable subdirectory, so one bad entry never aborts indexing. This order is
// the graph's canonical order — Phase B assigns NodeIDs by walking it — so it is
// fixed here, once, before any parallelism.
std::vector<std::filesystem::path> crawl(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> paths;
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      dir, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const std::filesystem::path& path = it->path();
    std::error_code stat_ec;
    if (!it->is_regular_file(stat_ec) || stat_ec || !is_c_source(path)) continue;
    paths.push_back(path);
  }
  return paths;
}

// Phases B–C — merge then resolve. Stitch the per-file facts into a graph and
// resolve its cross-file references. Runs in full whatever produced `parsed`, so
// an incremental reindex yields exactly the same graph as a from-scratch one.
Graph build_graph(const std::vector<ParsedFile>& parsed, unsigned workers) {
  // ── Phase B — merge (sequential) ───────────────────────────────────────
  // A single thread stitches the per-file facts into the graph, assigning
  // NodeIDs and building the global symbol table (ADR-0006). Names that can only
  // be resolved once every file is known are deferred to Phase C as `pending`
  // records that carry the already-known endpoint node and the unresolved name.
  Graph graph;

  // A callee may be defined in a file merged later, so calls are recorded with
  // their known caller node and resolved to definition nodes in a later pass.
  struct PendingCall {
    NodeId caller;
    std::string callee;
    std::uint32_t line;  // 1-based line of the call site, for diagnostics
  };
  std::vector<PendingCall> pending;

  // FunctionDecl nodes to link once every definition is known, for the same
  // reason: a prototype's defining function may live in a file merged later.
  std::vector<NodeId> pending_decls;

  // Type references, resolved in a later pass: a function may reference a type
  // declared in a header merged later, so we record the (already-known) using
  // function node with the still-unresolved type name.
  struct PendingTypeUse {
    NodeId user;
    std::string type;
    std::uint32_t line;  // 1-based line of the reference, for future diagnostics
  };
  std::vector<PendingTypeUse> pending_type_uses;

  // Include directives, resolved in a later pass: an included file may be merged
  // after the file that includes it, so we defer until every File node exists.
  // `file_by_path` maps each indexed file's normalized path to its File node.
  struct PendingInclude {
    NodeId includer;
    std::string target;
    bool is_system;
    std::uint32_t line;
  };
  std::vector<PendingInclude> pending_includes;
  std::unordered_map<std::string, NodeId> file_by_path;

  for (const ParsedFile& pf : parsed) {
    if (pf.status == ParsedFile::Status::Unreadable) {
      graph.add_skipped_file(SkippedFile{pf.path.string(), "unreadable"});
      continue;
    }
    if (pf.status == ParsedFile::Status::SyntaxError) {
      graph.add_skipped_file(SkippedFile{pf.path.string(), "syntax error"});
      continue;
    }

    // A File node per indexed file — an INCLUDES endpoint — keyed by normalized
    // path so an `#include` can resolve to it. The name is the basename, which
    // is how include-graph looks a file up; the full path lives in `file`.
    const NodeId file_id = graph.add_node(Node{NodeKind::File,
                                               pf.path.filename().string(),
                                               pf.path.string(), 0,
                                               Linkage::External, pf.hash});
    file_by_path.emplace(pf.path.lexically_normal().string(), file_id);

    for (const IncludeFact& inc : pf.includes) {
      pending_includes.push_back(
          {file_id, inc.target, inc.is_system, inc.line});
    }

    // Definitions first, so a call can be attributed to the node of its
    // enclosing function. Function names are unique within a C translation
    // unit, so name -> node is unambiguous within this file.
    std::unordered_map<std::string, NodeId> local_defs;
    for (const DefinitionFact& fact : pf.definitions) {
      const Linkage linkage =
          fact.is_static ? Linkage::Internal : Linkage::External;
      const NodeId id = graph.add_node(Node{NodeKind::Function, fact.name,
                                            pf.path.string(), fact.line, linkage});
      local_defs.emplace(fact.name, id);
    }

    for (const CallFact& call : pf.calls) {
      const auto caller = local_defs.find(call.caller);
      if (caller == local_defs.end()) continue;  // enclosing def not indexed
      pending.push_back({caller->second, call.callee, call.line});
    }

    // Header prototypes become FunctionDecl nodes, distinct from definitions.
    // Their linkage field is unused (prototypes carry no storage of their own);
    // External is the neutral default and keeps them out of static shadowing.
    for (const DeclarationFact& decl : pf.declarations) {
      const NodeId id = graph.add_node(Node{NodeKind::FunctionDecl, decl.name,
                                            pf.path.string(), decl.line,
                                            Linkage::External});
      pending_decls.push_back(id);
    }

    // User-defined types become typed nodes. Linkage is meaningless for a type;
    // External is the neutral default, matching the other non-function nodes.
    for (const TypeFact& type : pf.types) {
      NodeKind kind = NodeKind::Struct;
      switch (type.category) {
        case TypeCategory::Struct:  kind = NodeKind::Struct;  break;
        case TypeCategory::Union:   kind = NodeKind::Union;   break;
        case TypeCategory::Enum:    kind = NodeKind::Enum;    break;
        case TypeCategory::Typedef: kind = NodeKind::Typedef; break;
      }
      graph.add_node(
          Node{kind, type.name, pf.path.string(), type.line, Linkage::External});
    }

    // Type references, attributed to their enclosing function (already a node in
    // this file). The type name is resolved to type node(s) in a later pass.
    for (const TypeUseFact& use : pf.type_uses) {
      const auto user = local_defs.find(use.function);
      if (user == local_defs.end()) continue;  // enclosing def not indexed
      pending_type_uses.push_back({user->second, use.type, use.line});
    }
  }

  // ── Phase C — resolve (parallel) ───────────────────────────────────────
  // Call sites are resolved against the now-immutable symbol table (ADR-0006).
  // Each pending call's resolution is independent and read-only over the graph,
  // so the work is spread across the pool into per-call result slots; the graph
  // edges are then applied by a single thread in the original pending order,
  // which keeps CALLS insertion order — and thus every query result — identical
  // to the single-threaded build.
  //
  // Linkage-aware resolution (ADR-0005): for each call we apply C's visibility
  // rules over the indexed definitions of the callee's name:
  //   1. A `static` definition in the caller's own file shadows everything: the
  //      call resolves to it alone.
  //   2. Otherwise the call resolves to the external-linkage definitions, which
  //      a static in some *other* file is invisible to. Zero of these leaves the
  //      call unresolved (no edge); more than one is a real C link error, kept
  //      as every edge plus a flagged diagnostic rather than a guess.
  struct CallResolution {
    std::vector<NodeId> edges;            // callee node ids to link, in order
    std::optional<Diagnostic> diagnostic;
  };
  std::vector<CallResolution> resolutions(pending.size());
  parallel_for(pending.size(), workers, [&](std::size_t i) {
    const PendingCall& call = pending[i];
    CallResolution& out = resolutions[i];
    const std::string_view caller_file = graph.node(call.caller).file;
    const std::vector<NodeId>& candidates = graph.nodes_named(call.callee);

    for (const NodeId id : candidates) {
      const NodeView def = graph.node(id);
      if (def.kind != NodeKind::Function) continue;  // a call binds to a body
      if (def.linkage == Linkage::Internal && def.file == caller_file) {
        out.edges.push_back(id);
        return;  // at most one static of a given name per translation unit
      }
    }

    for (const NodeId id : candidates) {
      const NodeView def = graph.node(id);
      if (def.kind == NodeKind::Function && def.linkage == Linkage::External) {
        out.edges.push_back(id);
      }
    }
    if (out.edges.size() > 1) {
      out.diagnostic =
          Diagnostic{call.callee, std::string(caller_file), call.line, out.edges};
    }
  });

  for (std::size_t i = 0; i < pending.size(); ++i) {
    for (const NodeId callee : resolutions[i].edges) {
      graph.add_edge(pending[i].caller, callee);
    }
    if (resolutions[i].diagnostic) {
      graph.add_diagnostic(std::move(*resolutions[i].diagnostic));
    }
  }

  // Link each declaration to the function it declares. A prototype declares an
  // external-linkage function, so we bind it to the unique external definition
  // of its name; failing that, to the sole definition of any linkage. Zero
  // definitions (a header-only prototype) or an ambiguous set leaves the
  // declaration unlinked rather than guessed at. Cheap and left sequential.
  for (const NodeId decl : pending_decls) {
    const std::vector<NodeId>& candidates =
        graph.nodes_named(graph.node(decl).name);

    std::vector<NodeId> externals;
    std::vector<NodeId> definitions;
    for (const NodeId id : candidates) {
      const NodeView def = graph.node(id);
      if (def.kind != NodeKind::Function) continue;
      definitions.push_back(id);
      if (def.linkage == Linkage::External) externals.push_back(id);
    }

    if (externals.size() == 1) {
      graph.link_declaration(decl, externals.front());
    } else if (externals.empty() && definitions.size() == 1) {
      graph.link_declaration(decl, definitions.front());
    }
  }

  // Resolve includes. A local `"..."` include is resolved like a C compiler
  // would: relative to the directory of the including file. A hit in the indexed
  // set becomes an INCLUDES edge; a miss — a system `<...>` header or a target
  // outside the indexed set — is recorded as unresolved, not linked, and never
  // errors (a project's external headers are simply not in the graph).
  for (const PendingInclude& inc : pending_includes) {
    if (!inc.is_system) {
      const std::filesystem::path includer_path{
          std::string(graph.node(inc.includer).file)};
      const std::string candidate =
          (includer_path.parent_path() / inc.target).lexically_normal().string();
      const auto found = file_by_path.find(candidate);
      if (found != file_by_path.end()) {
        graph.add_include(inc.includer, found->second);
        continue;
      }
    }
    graph.add_unresolved_include(
        UnresolvedInclude{inc.includer, inc.target, inc.is_system, inc.line});
  }

  // Resolve type references. A referenced name binds to every type node that
  // carries it: usually one, but a tag and typedef sharing a name
  // (`typedef struct Point { ... } Point;`) are both legitimate targets. Names
  // with no type node — a built-in slipped through, or a type declared outside
  // the indexed set — bind to nothing and form no edge, never an error.
  for (const PendingTypeUse& use : pending_type_uses) {
    for (const NodeId id : graph.nodes_named(use.type)) {
      if (is_type_node(graph.node(id).kind)) graph.add_uses_type(use.user, id);
    }
  }
  return graph;
}

}  // namespace

IndexResult reindex_directory(const std::filesystem::path& dir,
                              const std::vector<ParsedFile>& prior,
                              const IndexOptions& opts) {
  const unsigned workers =
      opts.threads == 0 ? default_thread_count() : opts.threads;

  const std::vector<std::filesystem::path> paths = crawl(dir);

  // Index the prior artifacts by path so an unchanged file's facts can be looked
  // up and reused without re-parsing. The map borrows `prior`, which outlives it.
  std::unordered_map<std::string, const ParsedFile*> prior_by_path;
  prior_by_path.reserve(prior.size());
  for (const ParsedFile& pf : prior) {
    prior_by_path.emplace(pf.path.string(), &pf);
  }

  std::vector<char> reused;
  std::vector<ParsedFile> parsed = parse_files(paths, prior_by_path, workers, reused);

  IndexResult result;
  result.graph = build_graph(parsed, workers);
  for (const char r : reused) (r ? result.files_reused : result.files_parsed) += 1;
  result.artifacts = std::move(parsed);
  return result;
}

Graph index_directory(const std::filesystem::path& dir, const IndexOptions& opts) {
  return reindex_directory(dir, {}, opts).graph;
}

}  // namespace cartograph
