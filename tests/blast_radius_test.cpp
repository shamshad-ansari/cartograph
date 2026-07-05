#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path blast_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "blast";
}

// The transitive callers of `name` as sorted "basename:line" strings — the blast
// radius, independent of absolute paths or traversal order. A caller reached by
// several paths appears once; the query target itself never appears.
std::vector<std::string> blast_radius(const cartograph::Graph& graph,
                                      const std::string& name) {
  std::set<std::pair<std::string, std::uint32_t>> seen;
  for (const cartograph::Caller& c :
       graph.transitive_callers(graph.nodes_named(name))) {
    const cartograph::NodeView node = graph.node(c.node);
    seen.emplace(std::filesystem::path(node.file).filename().string(),
                 node.line);
  }
  std::vector<std::string> out;
  for (const auto& [file, line] : seen) {
    out.push_back(file + ":" + std::to_string(line));
  }
  return out;
}

// Shortest hop count from `name` to each transitive caller, keyed by its
// "basename:line" — lets a test assert that depth is the length of the *nearest*
// path, not just any path.
std::map<std::string, std::uint32_t> depths(const cartograph::Graph& graph,
                                            const std::string& name) {
  std::map<std::string, std::uint32_t> out;
  for (const cartograph::Caller& c :
       graph.transitive_callers(graph.nodes_named(name))) {
    const cartograph::NodeView node = graph.node(c.node);
    const std::string key =
        std::filesystem::path(node.file).filename().string() + ":" +
        std::to_string(node.line);
    out.emplace(key, c.depth);
  }
  return out;
}

}  // namespace

// A leaf of a straight call chain (top -> mid -> leaf) reports the full transitive
// caller set — the direct caller and the indirect one two hops out — and each
// carries its shortest distance from the leaf.
TEST(BlastRadius, MultiHopChainReportsAllCallersWithDepth) {
  const cartograph::Graph graph = cartograph::index_directory(blast_dir());

  EXPECT_EQ(blast_radius(graph, "leaf"),
            (std::vector<std::string>{"chain.c:7", "chain.c:11", "cycle.c:7",
                                      "cycle.c:11"}));

  const std::map<std::string, std::uint32_t> d = depths(graph, "leaf");
  EXPECT_EQ(d.at("chain.c:7"), 1u);   // mid: direct caller
  EXPECT_EQ(d.at("chain.c:11"), 2u);  // top: mid's caller
  EXPECT_EQ(d.at("cycle.c:7"), 1u);   // pong: direct caller in another file
}

// A mid-chain function's radius excludes itself and stops short of the leaf below
// it: only what sits above mid is affected when mid changes.
TEST(BlastRadius, StopsAtTheQueriedNode) {
  const cartograph::Graph graph = cartograph::index_directory(blast_dir());
  EXPECT_EQ(blast_radius(graph, "mid"),
            (std::vector<std::string>{"chain.c:11"}));  // top only
}

// The ping <-> pong cycle must not trap the reverse traversal. Both are indirect
// callers of leaf; each appears exactly once despite the cycle between them.
TEST(BlastRadius, CyclesTerminateAndDedupe) {
  const cartograph::Graph graph = cartograph::index_directory(blast_dir());

  const std::vector<std::string> radius = blast_radius(graph, "leaf");
  EXPECT_EQ(std::count(radius.begin(), radius.end(), "cycle.c:7"), 1);
  EXPECT_EQ(std::count(radius.begin(), radius.end(), "cycle.c:11"), 1);

  // Querying inside the cycle: pong's callers are ping (direct) and pong itself
  // via ping — but the seed is excluded, so only ping is reported.
  EXPECT_EQ(blast_radius(graph, "pong"),
            (std::vector<std::string>{"cycle.c:11"}));  // ping only
}

// A function nobody calls has an empty blast radius.
TEST(BlastRadius, UncalledFunctionHasEmptyRadius) {
  const cartograph::Graph graph = cartograph::index_directory(blast_dir());
  EXPECT_TRUE(blast_radius(graph, "top").empty());
}
