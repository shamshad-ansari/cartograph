#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path defs_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "defs";
}

// The definition locations for `name` as sorted "basename:line" strings, so the
// assertions don't depend on absolute paths or directory iteration order.
std::vector<std::string> locations(const cartograph::Graph& graph,
                                   const std::string& name) {
  std::vector<std::string> out;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    const cartograph::NodeView node = graph.node(id);
    out.push_back(std::filesystem::path(node.file).filename().string() + ":" +
                  std::to_string(node.line));
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace

TEST(FindDefinition, LocatesFunctionsAcrossDirectory) {
  const cartograph::Graph graph = cartograph::index_directory(defs_dir());

  // Same name defined in two files -> both definitions found.
  EXPECT_EQ(locations(graph, "add"),
            (std::vector<std::string>{"extra.c:1", "math.c:3"}));
  EXPECT_EQ(locations(graph, "sub"), (std::vector<std::string>{"math.c:7"}));
  // Pointer-returning function: name lives under a pointer_declarator.
  EXPECT_EQ(locations(graph, "label"), (std::vector<std::string>{"math.c:11"}));
  // Definitions in .h files are indexed too.
  EXPECT_EQ(locations(graph, "helper"), (std::vector<std::string>{"util.h:4"}));
}

TEST(FindDefinition, UnknownNameHasNoDefinitions) {
  const cartograph::Graph graph = cartograph::index_directory(defs_dir());
  EXPECT_TRUE(graph.nodes_named("does_not_exist").empty());
}
