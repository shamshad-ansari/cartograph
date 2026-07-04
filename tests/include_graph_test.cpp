#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path includes_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "includes";
}

// The File node whose basename is `name`, or nullopt if none is indexed.
std::optional<cartograph::NodeId> file_node(const cartograph::Graph& graph,
                                            const std::string& name) {
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    if (graph.node(id).kind == cartograph::NodeKind::File) return id;
  }
  return std::nullopt;
}

// Basenames of the files `name` directly includes (resolved edges), sorted.
std::vector<std::string> includees(const cartograph::Graph& graph,
                                   const std::string& name) {
  std::set<std::string> seen;
  for (const cartograph::NodeId id : graph.includes_of(*file_node(graph, name))) {
    seen.insert(std::filesystem::path(graph.node(id).file).filename().string());
  }
  return {seen.begin(), seen.end()};
}

// Basenames of the files that directly include `name`, sorted.
std::vector<std::string> includers(const cartograph::Graph& graph,
                                   const std::string& name) {
  std::set<std::string> seen;
  for (const cartograph::NodeId id : graph.included_by(*file_node(graph, name))) {
    seen.insert(std::filesystem::path(graph.node(id).file).filename().string());
  }
  return {seen.begin(), seen.end()};
}

// The (target, is_system) pairs of `name`'s unresolved includes, sorted.
std::set<std::pair<std::string, bool>> unresolved(const cartograph::Graph& graph,
                                                  const std::string& name) {
  const cartograph::NodeId node = *file_node(graph, name);
  std::set<std::pair<std::string, bool>> out;
  for (const cartograph::UnresolvedInclude& u : graph.unresolved_includes()) {
    if (u.includer == node) out.emplace(u.target, u.is_system);
  }
  return out;
}

}  // namespace

// Local `"..."` includes that resolve within the indexed set become INCLUDES
// edges, traversable in both directions across the multi-file set.
TEST(IncludeGraph, ResolvesLocalIncludesBothDirections) {
  const cartograph::Graph graph = cartograph::index_directory(includes_dir());

  EXPECT_EQ(includees(graph, "app.c"),
            (std::vector<std::string>{"config.h", "util.h"}));
  EXPECT_EQ(includees(graph, "util.h"),
            (std::vector<std::string>{"config.h"}));
  EXPECT_TRUE(includees(graph, "config.h").empty());

  // config.h is included by both app.c and util.h; util.h only by app.c.
  EXPECT_EQ(includers(graph, "config.h"),
            (std::vector<std::string>{"app.c", "util.h"}));
  EXPECT_EQ(includers(graph, "util.h"),
            (std::vector<std::string>{"app.c"}));
  EXPECT_TRUE(includers(graph, "app.c").empty());
}

// A system `<...>` header and a local include with no file in the set are both
// recorded as unresolved and form no edge — no error, no phantom File node.
TEST(IncludeGraph, RecordsSystemAndMissingIncludesAsUnresolved) {
  const cartograph::Graph graph = cartograph::index_directory(includes_dir());

  EXPECT_EQ(unresolved(graph, "app.c"),
            (std::set<std::pair<std::string, bool>>{
                {"stdio.h", true},    // <stdio.h> — system
                {"missing.h", false}  // "missing.h" — local, not in the set
            }));

  // The unresolved targets did not leak in as File nodes.
  EXPECT_FALSE(file_node(graph, "stdio.h").has_value());
  EXPECT_FALSE(file_node(graph, "missing.h").has_value());
}
