// Issue 0016: incremental reindex. A reindex reuses the parse artifacts of any
// file whose content hash is unchanged and re-parses only the rest, then runs a
// full merge + resolve. These tests pin the observable contract: unchanged files
// are not re-parsed, the resulting graph is identical to a from-scratch index,
// and a cross-file resolution change from an edit is reflected after reindex.
#include "cartograph/indexer.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"

namespace {

namespace fs = std::filesystem;

using cartograph::Graph;
using cartograph::NodeId;

// A scratch directory of source files, removed recursively on destruction so a
// test leaves nothing behind whether it passes or throws.
struct TempTree {
  fs::path dir;
  TempTree()
      : dir(fs::temp_directory_path() /
            ("cartograph_incr_test_" +
             std::to_string(::testing::UnitTest::GetInstance()->random_seed() ^
                            reinterpret_cast<std::uintptr_t>(this)))) {
    fs::create_directories(dir);
  }
  ~TempTree() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  // Write `contents` to `name` under the tree, creating or truncating it.
  void write(const std::string& name, const std::string& contents) const {
    std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }
};

// The sorted (file, line) pairs the queries actually compare — the order-
// independent form, so equality pins the observable answer, not a vector order.
std::set<std::pair<std::string, std::uint32_t>> located(
    const Graph& g, const std::vector<NodeId>& ids) {
  std::set<std::pair<std::string, std::uint32_t>> out;
  for (const NodeId id : ids) {
    const auto n = g.node(id);
    out.emplace(std::string(n.file), n.line);
  }
  return out;
}

// Assert `a` and `b` answer every query kind identically for every node and
// name — the golden check that an incremental reindex reproduces a from-scratch
// index exactly.
void expect_graph_equivalent(const Graph& a, const Graph& b) {
  ASSERT_EQ(a.size(), b.size());
  ASSERT_EQ(a.edge_count(), b.edge_count());

  std::set<std::string> names;
  for (NodeId id = 0; id < a.size(); ++id) {
    const auto na = a.node(id);
    const auto nb = b.node(id);
    EXPECT_EQ(nb.kind, na.kind) << "node " << id;
    EXPECT_EQ(nb.name, na.name) << "node " << id;
    EXPECT_EQ(nb.file, na.file) << "node " << id;
    EXPECT_EQ(nb.line, na.line) << "node " << id;
    EXPECT_EQ(nb.linkage, na.linkage) << "node " << id;
    EXPECT_EQ(nb.hash, na.hash) << "node " << id;

    EXPECT_EQ(located(a, a.callers_of(id)), located(b, b.callers_of(id)))
        << "callers of " << id;
    EXPECT_EQ(located(a, a.includes_of(id)), located(b, b.includes_of(id)))
        << "includes of " << id;
    EXPECT_EQ(located(a, a.included_by(id)), located(b, b.included_by(id)))
        << "included by " << id;
    EXPECT_EQ(located(a, a.users_of(id)), located(b, b.users_of(id)))
        << "users of " << id;
    EXPECT_EQ(a.definition_of(id), b.definition_of(id)) << "definition of " << id;
    names.insert(std::string(na.name));
  }
  for (const std::string& name : names) {
    EXPECT_EQ(a.nodes_named(name), b.nodes_named(name)) << "nodes named " << name;
  }
}

// The Function definition node named `name`, or an assertion failure if none.
NodeId function_node(const Graph& g, const std::string& name) {
  for (const NodeId id : g.nodes_named(name)) {
    if (g.node(id).kind == cartograph::NodeKind::Function) return id;
  }
  ADD_FAILURE() << "no Function node named " << name;
  return 0;
}

}  // namespace

// A reindex re-parses only the files whose content changed; unchanged files are
// served from the prior run's artifacts.
TEST(Incremental, ReparsesOnlyChangedFiles) {
  TempTree tree;
  tree.write("a.c", "void fa(void) { }\n");
  tree.write("b.c", "void fb(void) { }\n");

  const cartograph::IndexResult first = cartograph::reindex_directory(tree.dir, {});
  EXPECT_EQ(first.files_parsed, 2u);
  EXPECT_EQ(first.files_reused, 0u);

  // Change only b.c; a.c is byte-for-byte identical.
  tree.write("b.c", "void fb(void) { int x = 1; (void)x; }\n");

  const cartograph::IndexResult second =
      cartograph::reindex_directory(tree.dir, first.artifacts);
  EXPECT_EQ(second.files_parsed, 1u) << "only b.c should be re-parsed";
  EXPECT_EQ(second.files_reused, 1u) << "a.c should be reused from artifacts";
}

// The graph an incremental reindex yields is identical to a full from-scratch
// index of the same tree, whatever was reused.
TEST(Incremental, ReindexedGraphMatchesFromScratch) {
  TempTree tree;
  tree.write("a.c", "void fa(void) { fb(); }\n");
  tree.write("b.c", "void fb(void) { }\n");

  const cartograph::IndexResult first = cartograph::reindex_directory(tree.dir, {});

  tree.write("a.c", "void fa(void) { fb(); fb(); }\n");

  const cartograph::IndexResult second =
      cartograph::reindex_directory(tree.dir, first.artifacts);
  const Graph scratch = cartograph::index_directory(tree.dir);
  expect_graph_equivalent(second.graph, scratch);
}

// Editing a definition that changes cross-file resolution is reflected after a
// reindex, even though the calling file is unchanged and reused: making a static
// callee external makes a caller in another file resolve to it.
TEST(Incremental, ReflectsCrossFileResolutionChange) {
  TempTree tree;
  tree.write("a.c", "void fa(void) { fb(); }\n");
  tree.write("b.c", "static void fb(void) { }\n");  // internal — invisible to a.c

  const cartograph::IndexResult first = cartograph::reindex_directory(tree.dir, {});
  {
    const Graph& g = first.graph;
    const NodeId fb = function_node(g, "fb");
    EXPECT_TRUE(g.callers_of(fb).empty())
        << "a static callee in another file is invisible to fa's call";
  }

  // Make fb external; only b.c changes, but a.c's call must now resolve to it.
  tree.write("b.c", "void fb(void) { }\n");

  const cartograph::IndexResult second =
      cartograph::reindex_directory(tree.dir, first.artifacts);
  EXPECT_EQ(second.files_reused, 1u) << "a.c is unchanged and reused";
  const Graph& g = second.graph;
  const NodeId fb = function_node(g, "fb");
  const auto callers = located(g, g.callers_of(fb));
  ASSERT_EQ(callers.size(), 1u) << "fa now resolves to the external fb";
  EXPECT_EQ(callers.begin()->first, (tree.dir / "a.c").string());
}
