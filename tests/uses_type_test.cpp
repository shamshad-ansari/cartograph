#include <algorithm>
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

std::filesystem::path types_dir() {
  return std::filesystem::path(FIXTURE_DIR) / "types";
}

// The distinct functions that reference the type(s) named `name`, as sorted
// "basename:line" strings — the answer to "who uses this type?". A function
// appears once even if it references the type repeatedly, independent of
// absolute paths or iteration order.
std::vector<std::string> users(const cartograph::Graph& graph,
                               const std::string& name) {
  std::set<std::pair<std::string, std::uint32_t>> seen;
  for (const cartograph::NodeId type : graph.nodes_named(name)) {
    if (!cartograph::is_type_node(graph.node(type).kind)) continue;
    for (const cartograph::NodeId user : graph.users_of(type)) {
      const cartograph::NodeView node = graph.node(user);
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

// The kind of the single type node named `name`, or nullopt if there is not
// exactly one type node with that name.
std::optional<cartograph::NodeKind> type_kind(const cartograph::Graph& graph,
                                              const std::string& name) {
  std::optional<cartograph::NodeKind> found;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    if (!cartograph::is_type_node(graph.node(id).kind)) continue;
    if (found) return std::nullopt;  // more than one — ambiguous
    found = graph.node(id).kind;
  }
  return found;
}

}  // namespace

// struct/union/enum definitions and typedefs are each extracted as a node of the
// matching kind.
TEST(UsesType, ExtractsEachTypeKind) {
  const cartograph::Graph graph = cartograph::index_directory(types_dir());

  EXPECT_EQ(type_kind(graph, "Point"), cartograph::NodeKind::Struct);
  EXPECT_EQ(type_kind(graph, "Color"), cartograph::NodeKind::Enum);
  EXPECT_EQ(type_kind(graph, "Value"), cartograph::NodeKind::Union);
  EXPECT_EQ(type_kind(graph, "Celsius"), cartograph::NodeKind::Typedef);
}

// A type's user set spans return types, parameters, and locals, and a function
// that touches no user-defined type is absent from every set.
TEST(UsesType, ListsReferencingFunctions) {
  const cartograph::Graph graph = cartograph::index_directory(types_dir());

  // struct Point: parameter in point_x, return type + local in origin.
  EXPECT_EQ(users(graph, "Point"),
            (std::vector<std::string>{"shapes.c:4", "shapes.c:9"}));

  // enum Color: parameter of origin only.
  EXPECT_EQ(users(graph, "Color"), (std::vector<std::string>{"shapes.c:9"}));

  // Celsius typedef: return type + local of freezing.
  EXPECT_EQ(users(graph, "Celsius"), (std::vector<std::string>{"shapes.c:17"}));
}

// USES_TYPE edges cross files: a function references a type declared in a header
// it includes.
TEST(UsesType, ResolvesAcrossFiles) {
  const cartograph::Graph graph = cartograph::index_directory(types_dir());

  EXPECT_EQ(users(graph, "Value"), (std::vector<std::string>{"app.c:4"}));
}

// `add` references only built-in types, which are not type nodes and form no
// USES_TYPE edge.
TEST(UsesType, BuiltinOnlyFunctionHasNoTypeEdges) {
  const cartograph::Graph graph = cartograph::index_directory(types_dir());

  for (const std::vector<std::string>& set :
       {users(graph, "Point"), users(graph, "Color"), users(graph, "Value"),
        users(graph, "Celsius")}) {
    EXPECT_EQ(std::count(set.begin(), set.end(), std::string("shapes.c:23")), 0);
  }
}
