#include "cartograph/indexer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cartograph/extractor.hpp"
#include "cartograph/parser.hpp"

namespace cartograph {
namespace {

// True for the C source and header extensions this slice indexes.
bool is_c_source(const std::filesystem::path& path) {
  const std::filesystem::path ext = path.extension();
  return ext == ".c" || ext == ".h";
}

// Read the whole file into `out`. Returns false if it cannot be opened.
bool read_file(const std::filesystem::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

}  // namespace

Graph index_directory(const std::filesystem::path& dir) {
  Graph graph;
  Parser parser;

  // A callee may be defined in a file indexed later, so calls can't be resolved
  // during the walk. We record each with its already-known caller node and
  // resolve names to definition nodes in a second pass.
  struct PendingCall {
    NodeId caller;
    std::string callee;
  };
  std::vector<PendingCall> pending;

  // Iterate the directory non-recursively using the error-code overloads so a
  // vanished or unreadable entry stops the walk cleanly rather than throwing.
  // Recursive crawling arrives in issue 0009.
  std::error_code ec;
  std::filesystem::directory_iterator it(dir, ec);
  const std::filesystem::directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const std::filesystem::path& path = it->path();
    if (!it->is_regular_file(ec) || !is_c_source(path)) continue;

    std::string source;
    if (!read_file(path, source)) continue;

    const Tree tree = parser.parse(source);

    // Definitions first, so a call can be attributed to the node of its
    // enclosing function. Function names are unique within a C translation
    // unit, so name -> node is unambiguous within this file.
    std::unordered_map<std::string, NodeId> local_defs;
    for (DefinitionFact& fact : extract_definitions(tree, source)) {
      const NodeId id = graph.add_node(
          Node{NodeKind::Function, fact.name, path.string(), fact.line});
      local_defs.emplace(std::move(fact.name), id);
    }

    for (CallFact& call : extract_calls(tree, source)) {
      const auto caller = local_defs.find(call.caller);
      if (caller == local_defs.end()) continue;  // enclosing def not indexed
      pending.push_back({caller->second, std::move(call.callee)});
    }
  }

  // Second pass — naive name resolution: a call links to every definition that
  // bears the callee's name across the whole file set. Linkage rules that would
  // pick a single target arrive in issue 0004.
  for (const PendingCall& call : pending) {
    for (const NodeId callee : graph.nodes_named(call.callee)) {
      graph.add_edge(call.caller, callee);
    }
  }
  return graph;
}

}  // namespace cartograph
