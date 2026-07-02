#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path calls_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "calls";
}

// The distinct caller functions of `name` as sorted "basename:line" strings —
// the answer to "who calls this?". A caller appears once even if it calls the
// target more than once, and independent of absolute paths or iteration order.
std::vector<std::string> callers(const cartograph::Graph& graph,
                                 const std::string& name) {
  std::set<std::pair<std::string, std::uint32_t>> seen;
  for (const cartograph::NodeId callee : graph.nodes_named(name)) {
    for (const cartograph::NodeId caller : graph.callers_of(callee)) {
      const cartograph::Node& node = graph.node(caller);
      seen.emplace(std::filesystem::path(node.file).filename().string(),
                   node.line);
    }
  }
  std::vector<std::string> out;
  for (const auto& [file, line] : seen) {
    out.push_back(file + ":" + std::to_string(line));
  }
  return out;
}

}  // namespace

// Callers are found across files, a callee with several callers reports all of
// them, and a call nested inside control flow (run -> compute, inside an `if`)
// is attributed to its enclosing function.
TEST(WhoCalls, ListsCallersAcrossFilesAndNesting) {
  const cartograph::Graph graph = cartograph::index_directory(calls_dir());

  EXPECT_EQ(callers(graph, "helper"),
            (std::vector<std::string>{"app.c:5", "app.c:13", "lib.c:1"}));
  EXPECT_EQ(callers(graph, "compute"), (std::vector<std::string>{"app.c:5"}));
  EXPECT_EQ(callers(graph, "run"), (std::vector<std::string>{"app.c:13"}));
}

TEST(WhoCalls, FunctionNobodyCallsHasNoCallers) {
  const cartograph::Graph graph = cartograph::index_directory(calls_dir());
  EXPECT_TRUE(callers(graph, "main").empty());
}

// Naive (lexical) resolution links a call to *every* definition bearing the
// callee's name — `shared` is defined in two files, and the single caller links
// to both. Linkage rules that would disambiguate arrive in issue 0004.
TEST(WhoCalls, NaiveResolutionLinksEveryDefinitionOfName) {
  const cartograph::Graph graph = cartograph::index_directory(calls_dir());

  const std::vector<cartograph::NodeId>& defs = graph.nodes_named("shared");
  ASSERT_EQ(defs.size(), 2u);
  for (const cartograph::NodeId def : defs) {
    const std::vector<cartograph::NodeId>& cs = graph.callers_of(def);
    ASSERT_EQ(cs.size(), 1u);
    EXPECT_EQ(graph.node(cs.front()).name, "caller_one");
  }
}
