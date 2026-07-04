#include "cartograph/graph.hpp"

#include <unordered_set>
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

std::size_t Graph::edge_count() const {
  std::size_t total = definition_by_decl_.size();  // DECLARES: one per decl
  for (const auto& [callee, callers] : callers_by_callee_) total += callers.size();
  for (const auto& [file, includees] : includees_by_file_) total += includees.size();
  for (const auto& [type, users] : users_by_type_) total += users.size();
  return total;
}

void Graph::add_edge(NodeId caller, NodeId callee) {
  callers_by_callee_[callee].push_back(caller);
}

const std::vector<NodeId>& Graph::callers_of(NodeId id) const {
  static const std::vector<NodeId> kNone;
  const auto it = callers_by_callee_.find(id);
  return it == callers_by_callee_.end() ? kNone : it->second;
}

std::vector<Caller> Graph::transitive_callers(
    const std::vector<NodeId>& seeds) const {
  // `visited` is seeded with the targets so a cycle back into one is dropped and
  // no node is emitted twice; a node's first visit is via a shortest path, so
  // the depth we record with it is minimal (breadth-first frontier expansion).
  std::unordered_set<NodeId> visited(seeds.begin(), seeds.end());
  std::vector<NodeId> frontier(seeds.begin(), seeds.end());
  std::vector<Caller> result;
  for (std::uint32_t depth = 1; !frontier.empty(); ++depth) {
    std::vector<NodeId> next;
    for (const NodeId node : frontier) {
      for (const NodeId caller : callers_of(node)) {
        if (visited.insert(caller).second) {
          result.push_back(Caller{caller, depth});
          next.push_back(caller);
        }
      }
    }
    frontier = std::move(next);
  }
  return result;
}

void Graph::add_include(NodeId includer, NodeId includee) {
  includees_by_file_[includer].push_back(includee);
  includers_by_file_[includee].push_back(includer);
}

const std::vector<NodeId>& Graph::includes_of(NodeId id) const {
  static const std::vector<NodeId> kNone;
  const auto it = includees_by_file_.find(id);
  return it == includees_by_file_.end() ? kNone : it->second;
}

const std::vector<NodeId>& Graph::included_by(NodeId id) const {
  static const std::vector<NodeId> kNone;
  const auto it = includers_by_file_.find(id);
  return it == includers_by_file_.end() ? kNone : it->second;
}

void Graph::add_uses_type(NodeId user, NodeId type) {
  users_by_type_[type].push_back(user);
}

const std::vector<NodeId>& Graph::users_of(NodeId id) const {
  static const std::vector<NodeId> kNone;
  const auto it = users_by_type_.find(id);
  return it == users_by_type_.end() ? kNone : it->second;
}

void Graph::add_unresolved_include(UnresolvedInclude include) {
  unresolved_includes_.push_back(std::move(include));
}

void Graph::link_declaration(NodeId decl, NodeId def) {
  definition_by_decl_[decl] = def;
}

std::optional<NodeId> Graph::definition_of(NodeId decl) const {
  const auto it = definition_by_decl_.find(decl);
  if (it == definition_by_decl_.end()) return std::nullopt;
  return it->second;
}

void Graph::add_diagnostic(Diagnostic diagnostic) {
  diagnostics_.push_back(std::move(diagnostic));
}

void Graph::add_skipped_file(SkippedFile skipped) {
  skipped_files_.push_back(std::move(skipped));
}

}  // namespace cartograph
