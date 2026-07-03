#include "cartograph/graph.hpp"

#include <utility>

namespace cartograph {

NodeId Graph::add_node(Node node) {
  const NodeId id = static_cast<NodeId>(nodes_.size());
  by_name_[node.name].push_back(id);
  nodes_.push_back(std::move(node));
  return id;
}

const std::vector<NodeId>& Graph::nodes_named(std::string_view name) const {
  static const std::vector<NodeId> kNone;
  const auto it = by_name_.find(std::string(name));
  return it == by_name_.end() ? kNone : it->second;
}

void Graph::add_edge(NodeId caller, NodeId callee) {
  callers_by_callee_[callee].push_back(caller);
}

const std::vector<NodeId>& Graph::callers_of(NodeId id) const {
  static const std::vector<NodeId> kNone;
  const auto it = callers_by_callee_.find(id);
  return it == callers_by_callee_.end() ? kNone : it->second;
}

void Graph::add_diagnostic(Diagnostic diagnostic) {
  diagnostics_.push_back(std::move(diagnostic));
}

}  // namespace cartograph
