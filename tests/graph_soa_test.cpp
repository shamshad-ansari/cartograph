// Issue 0013: the graph stores nodes as a struct-of-arrays with names/paths in a
// single string arena. These tests pin the observable contract of that layout —
// node attributes still round-trip, and a path shared by many nodes is stored in
// the arena exactly once (referenced by offset/length, not copied per node).
#include "cartograph/graph.hpp"

#include <gtest/gtest.h>

namespace {

using cartograph::Graph;
using cartograph::Linkage;
using cartograph::Node;
using cartograph::NodeId;
using cartograph::NodeKind;

TEST(GraphSoa, NodeAttributesRoundTrip) {
  Graph graph;
  const NodeId id = graph.add_node(
      Node{NodeKind::Function, "handle_request", "src/server.c", 42,
           Linkage::Internal});

  const auto n = graph.node(id);
  EXPECT_EQ(n.kind, NodeKind::Function);
  EXPECT_EQ(n.name, "handle_request");
  EXPECT_EQ(n.file, "src/server.c");
  EXPECT_EQ(n.line, 42u);
  EXPECT_EQ(n.linkage, Linkage::Internal);
}

TEST(GraphSoa, FileNodeHashRoundTrips) {
  Graph graph;
  const NodeId id = graph.add_node(
      Node{NodeKind::File, "src/server.c", "src/server.c", 0,
           Linkage::External, 0xDEADBEEFull});
  EXPECT_EQ(graph.node(id).hash, 0xDEADBEEFull);
}

TEST(GraphSoa, SharedFilePathStoredOnceInArena) {
  Graph graph;
  // Two definitions in the same translation unit share one file path. In the
  // struct-of-arrays layout that path lives once in the arena; both nodes point
  // at the identical bytes, so the views compare pointer-equal.
  const NodeId a = graph.add_node(
      Node{NodeKind::Function, "first", "src/big_module.c", 1, Linkage::External});
  const NodeId b = graph.add_node(
      Node{NodeKind::Function, "second", "src/big_module.c", 2, Linkage::External});

  EXPECT_EQ(graph.node(a).file, "src/big_module.c");
  EXPECT_EQ(graph.node(b).file, "src/big_module.c");
  EXPECT_EQ(graph.node(a).file.data(), graph.node(b).file.data());
}

TEST(GraphSoa, DistinctNamesReadBackDistinct) {
  Graph graph;
  const NodeId a =
      graph.add_node(Node{NodeKind::Function, "alpha", "a.c", 1, Linkage::External});
  const NodeId b =
      graph.add_node(Node{NodeKind::Function, "beta", "b.c", 1, Linkage::External});

  EXPECT_EQ(graph.node(a).name, "alpha");
  EXPECT_EQ(graph.node(b).name, "beta");
  EXPECT_EQ(graph.size(), 2u);
}

}  // namespace
