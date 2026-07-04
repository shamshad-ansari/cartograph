#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"

namespace {

std::filesystem::path declarations_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "declarations";
}

// The FunctionDecl nodes named `name` as sorted "basename:line" strings — the
// answer to "where is this declared?". Definitions of the same name are
// excluded, so a header prototype is reported distinctly from its .c body.
std::vector<std::string> declarations(const cartograph::Graph& graph,
                                      const std::string& name) {
  std::vector<std::string> out;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    const cartograph::Node& node = graph.node(id);
    if (node.kind != cartograph::NodeKind::FunctionDecl) continue;
    out.push_back(std::filesystem::path(node.file).filename().string() + ":" +
                  std::to_string(node.line));
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The single FunctionDecl node named `name`. Fails the test unless exactly one
// exists, so a miswired fixture surfaces clearly.
cartograph::NodeId decl_node(const cartograph::Graph& graph,
                             const std::string& name) {
  std::vector<cartograph::NodeId> matches;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    if (graph.node(id).kind == cartograph::NodeKind::FunctionDecl) {
      matches.push_back(id);
    }
  }
  EXPECT_EQ(matches.size(), 1u) << "expected exactly one declaration of " << name;
  return matches.empty() ? cartograph::NodeId{0} : matches.front();
}

}  // namespace

// Header prototypes become FunctionDecl nodes, distinct from the Function
// definitions of the same name. `area` is both declared (shape.h) and defined
// (shape.c); the declaration and definition are separate nodes.
TEST(Declarations, HeaderPrototypesAreExtractedAsDeclNodes) {
  const cartograph::Graph graph =
      cartograph::index_directory(declarations_dir());

  EXPECT_EQ(declarations(graph, "area"),
            (std::vector<std::string>{"shape.h:6"}));
  // Pointer-returning prototype is captured too.
  EXPECT_EQ(declarations(graph, "name"),
            (std::vector<std::string>{"shape.h:9"}));
  EXPECT_EQ(declarations(graph, "perimeter"),
            (std::vector<std::string>{"shape.h:13"}));

  // The definition in shape.c is a separate Function node, not swallowed by the
  // declaration.
  bool has_definition = false;
  for (const cartograph::NodeId id : graph.nodes_named("area")) {
    if (graph.node(id).kind == cartograph::NodeKind::Function) {
      has_definition = true;
      EXPECT_EQ(std::filesystem::path(graph.node(id).file).filename(),
                "shape.c");
    }
  }
  EXPECT_TRUE(has_definition) << "area's definition must still be a Function node";
}

// A declaration is linked to its matching definition when one is indexed.
TEST(Declarations, DeclarationLinksToItsDefinition) {
  const cartograph::Graph graph =
      cartograph::index_directory(declarations_dir());

  const cartograph::NodeId decl = decl_node(graph, "area");
  const std::optional<cartograph::NodeId> def = graph.definition_of(decl);
  ASSERT_TRUE(def.has_value()) << "area's declaration must link to a definition";

  const cartograph::Node& node = graph.node(*def);
  EXPECT_EQ(node.kind, cartograph::NodeKind::Function);
  EXPECT_EQ(std::filesystem::path(node.file).filename(), "shape.c");
  EXPECT_EQ(node.line, 3u);
}

// A declaration with no definition in the indexed set stays unlinked rather
// than being guessed at.
TEST(Declarations, UndefinedDeclarationHasNoLink) {
  const cartograph::Graph graph =
      cartograph::index_directory(declarations_dir());

  const cartograph::NodeId decl = decl_node(graph, "perimeter");
  EXPECT_FALSE(graph.definition_of(decl).has_value())
      << "perimeter is declared but never defined";
}
