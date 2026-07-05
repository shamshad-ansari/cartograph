// Issue 0014 / ADR-0008: the struct-of-arrays graph and its string arena persist
// to a single on-disk file that a warm start memory-maps and serves queries from
// without re-parsing. These tests pin the observable contract: a loaded graph
// answers every query kind identically to the freshly-indexed one, and the
// format's magic/version header rejects stale or foreign files.
#include "cartograph/graph_io.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

namespace fs = std::filesystem;

using cartograph::Graph;
using cartograph::NodeId;

// A scratch path that is removed on destruction, so a test leaves no index file
// behind whether it passes or throws.
struct TempIndexFile {
  fs::path path;
  TempIndexFile()
      : path(fs::temp_directory_path() /
             ("cartograph_io_test_" +
              std::to_string(
                  ::testing::UnitTest::GetInstance()->random_seed() ^
                  reinterpret_cast<std::uintptr_t>(this)) +
              ".idx")) {}
  ~TempIndexFile() {
    std::error_code ec;
    fs::remove(path, ec);
  }
};

// The sorted (file, line) pairs a query command would print for `callers` — the
// order-independent form every query is actually consumed in, so comparing this
// pins the observable answer rather than an incidental vector order.
std::set<std::pair<std::string, std::uint32_t>> located(
    const Graph& g, const std::vector<NodeId>& ids) {
  std::set<std::pair<std::string, std::uint32_t>> out;
  for (const NodeId id : ids) {
    const auto n = g.node(id);
    out.emplace(std::string(n.file), n.line);
  }
  return out;
}

// Assert that `loaded` answers every query kind exactly as `original` does, for
// every node and name in the graph. This is the golden-snapshot regression for
// persistence: if a warm load diverged anywhere, one of these fires.
void expect_query_equivalent(const Graph& original, const Graph& loaded) {
  ASSERT_EQ(loaded.size(), original.size());
  ASSERT_EQ(loaded.edge_count(), original.edge_count());

  std::set<std::string> names;
  for (NodeId id = 0; id < original.size(); ++id) {
    const auto a = original.node(id);
    const auto b = loaded.node(id);
    EXPECT_EQ(b.kind, a.kind) << "node " << id;
    EXPECT_EQ(b.name, a.name) << "node " << id;
    EXPECT_EQ(b.file, a.file) << "node " << id;
    EXPECT_EQ(b.line, a.line) << "node " << id;
    EXPECT_EQ(b.linkage, a.linkage) << "node " << id;
    EXPECT_EQ(b.hash, a.hash) << "node " << id;

    EXPECT_EQ(located(loaded, loaded.callers_of(id)),
              located(original, original.callers_of(id)))
        << "callers of " << id;
    EXPECT_EQ(located(loaded, loaded.includes_of(id)),
              located(original, original.includes_of(id)))
        << "includes of " << id;
    EXPECT_EQ(located(loaded, loaded.included_by(id)),
              located(original, original.included_by(id)))
        << "included by " << id;
    EXPECT_EQ(located(loaded, loaded.users_of(id)),
              located(original, original.users_of(id)))
        << "users of " << id;
    EXPECT_EQ(loaded.definition_of(id), original.definition_of(id))
        << "definition of " << id;
    names.insert(std::string(a.name));
  }

  // find-definition / who-uses-type reach nodes through the name index.
  for (const std::string& name : names) {
    EXPECT_EQ(loaded.nodes_named(name), original.nodes_named(name))
        << "nodes named " << name;
  }

  // blast-radius walks the reverse call closure; seed it from every name.
  for (const std::string& name : names) {
    const auto a = original.transitive_callers(original.nodes_named(name));
    const auto b = loaded.transitive_callers(loaded.nodes_named(name));
    std::set<std::pair<std::string, std::uint32_t>> ra, rb;
    for (const auto& c : a) ra.emplace(std::string(original.node(c.node).file),
                                       original.node(c.node).line);
    for (const auto& c : b) rb.emplace(std::string(loaded.node(c.node).file),
                                       loaded.node(c.node).line);
    EXPECT_EQ(rb, ra) << "blast radius of " << name;
  }

  // include-graph also surfaces unresolved (system/external) includes.
  auto unresolved = [](const Graph& g) {
    std::multiset<std::tuple<NodeId, std::string, bool, std::uint32_t>> out;
    for (const auto& u : g.unresolved_includes())
      out.emplace(u.includer, u.target, u.is_system, u.line);
    return out;
  };
  EXPECT_EQ(unresolved(loaded), unresolved(original));

  // Diagnostics and skipped files feed the index summary and query warnings.
  ASSERT_EQ(loaded.diagnostics().size(), original.diagnostics().size());
  for (std::size_t i = 0; i < original.diagnostics().size(); ++i) {
    const auto& a = original.diagnostics()[i];
    const auto& b = loaded.diagnostics()[i];
    EXPECT_EQ(b.callee, a.callee);
    EXPECT_EQ(b.caller_file, a.caller_file);
    EXPECT_EQ(b.caller_line, a.caller_line);
    EXPECT_EQ(b.candidates, a.candidates);
  }
  auto skipped = [](const Graph& g) {
    std::multiset<std::pair<std::string, std::string>> out;
    for (const auto& s : g.skipped_files()) out.emplace(s.path, s.reason);
    return out;
  };
  EXPECT_EQ(skipped(loaded), skipped(original));
}

TEST(GraphIo, WarmLoadServesEveryQueryIdentically) {
  const Graph original = cartograph::index_directory(fs::path(FIXTURE_DIR) / "eval");
  ASSERT_GT(original.size(), 0u);

  TempIndexFile tmp;
  cartograph::save_graph(original, tmp.path);
  ASSERT_TRUE(fs::exists(tmp.path));
  ASSERT_GT(fs::file_size(tmp.path), 0u);

  const Graph loaded = cartograph::load_graph(tmp.path);
  expect_query_equivalent(original, loaded);
}

TEST(GraphIo, LoadedArenaIsViewedInThePlaceOfTheMapping) {
  // A warm load must reference the arena bytes in the mmap, not re-intern them:
  // two nodes sharing a file path still resolve to the identical bytes, exactly
  // as in the freshly-built graph (the struct-of-arrays contract, issue 0013).
  const Graph original = cartograph::index_directory(fs::path(FIXTURE_DIR) / "includes");
  TempIndexFile tmp;
  cartograph::save_graph(original, tmp.path);
  const Graph loaded = cartograph::load_graph(tmp.path);

  // Find a file path shared by at least two nodes and assert pointer identity.
  bool checked = false;
  for (NodeId a = 0; a < loaded.size() && !checked; ++a) {
    for (NodeId b = a + 1; b < loaded.size(); ++b) {
      if (loaded.node(a).file == loaded.node(b).file &&
          !loaded.node(a).file.empty()) {
        EXPECT_EQ(loaded.node(a).file.data(), loaded.node(b).file.data());
        checked = true;
        break;
      }
    }
  }
  EXPECT_TRUE(checked) << "fixture should share a path across nodes";
}

TEST(GraphIo, RejectsFileWithoutTheMagic) {
  TempIndexFile tmp;
  {
    std::ofstream out(tmp.path, std::ios::binary);
    const std::string junk = "not a cartograph index file, just some bytes here";
    out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
  }
  EXPECT_THROW(cartograph::load_graph(tmp.path), std::runtime_error);
}

TEST(GraphIo, RejectsAnIncompatibleVersion) {
  const Graph original = cartograph::index_directory(fs::path(FIXTURE_DIR) / "eval");
  TempIndexFile tmp;
  cartograph::save_graph(original, tmp.path);

  // Bump the version field (bytes 8..12, just past the 8-byte magic) to a value
  // this build does not understand; the loader must refuse it rather than read a
  // stale/foreign layout as if it were current.
  std::uint32_t bogus = 0xFFFFFFFFu;
  std::fstream f(tmp.path, std::ios::binary | std::ios::in | std::ios::out);
  f.seekp(8);
  f.write(reinterpret_cast<const char*>(&bogus), sizeof(bogus));
  f.close();

  EXPECT_THROW(cartograph::load_graph(tmp.path), std::runtime_error);
}

TEST(GraphIo, MissingFileThrows) {
  EXPECT_THROW(cartograph::load_graph("/no/such/cartograph.idx"),
               std::runtime_error);
}

}  // namespace
