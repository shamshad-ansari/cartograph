#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cartograph {

// Nodes are addressed by a dense integer id into a flat array, never by pointer
// (ADR-0007): this is cache-friendly for the traversal-heavy queries added in
// later slices and position-independent, a prerequisite for mmap persistence.
using NodeId = std::uint32_t;

// The kind of program entity a node represents. This slice records function
// definitions only; later slices add more kinds.
enum class NodeKind {
  Function,
};

// One entity in the code graph. For this slice: a function definition and where
// it was found.
struct Node {
  NodeKind kind;
  std::string name;
  std::string file;         // path as indexed
  std::uint32_t line;       // 1-based line of the definition's name
};

// In-memory code graph. Nodes live in a flat vector addressed by NodeId, with a
// secondary index from name to the nodes that carry it — the name index that
// answers find-definition. Kept deliberately simple (a vector graph) ahead of
// the struct-of-arrays migration in issue 0013.
class Graph {
 public:
  // Append a node and index it by name. Returns its NodeId.
  NodeId add_node(Node node);

  const Node& node(NodeId id) const { return nodes_[id]; }
  std::size_t size() const noexcept { return nodes_.size(); }

  // NodeIds whose name equals `name`, in insertion order; empty if none.
  const std::vector<NodeId>& nodes_named(std::string_view name) const;

 private:
  std::vector<Node> nodes_;
  std::unordered_map<std::string, std::vector<NodeId>> by_name_;
};

}  // namespace cartograph
