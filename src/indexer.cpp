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
    std::uint32_t line;  // 1-based line of the call site, for diagnostics
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
      const Linkage linkage =
          fact.is_static ? Linkage::Internal : Linkage::External;
      const NodeId id = graph.add_node(
          Node{NodeKind::Function, fact.name, path.string(), fact.line, linkage});
      local_defs.emplace(std::move(fact.name), id);
    }

    for (CallFact& call : extract_calls(tree, source)) {
      const auto caller = local_defs.find(call.caller);
      if (caller == local_defs.end()) continue;  // enclosing def not indexed
      pending.push_back({caller->second, std::move(call.callee), call.line});
    }
  }

  // Second pass — linkage-aware resolution (ADR-0005). For each call we apply
  // C's visibility rules over the indexed definitions of the callee's name:
  //
  //   1. A `static` definition in the caller's own file shadows everything: the
  //      call resolves to it alone.
  //   2. Otherwise the call resolves to the external-linkage definitions, which
  //      a static in some *other* file is invisible to. Zero of these leaves
  //      the call unresolved (no edge).
  for (const PendingCall& call : pending) {
    const std::string& caller_file = graph.node(call.caller).file;
    const std::vector<NodeId>& candidates = graph.nodes_named(call.callee);

    bool bound_local = false;
    for (const NodeId id : candidates) {
      const Node& def = graph.node(id);
      if (def.linkage == Linkage::Internal && def.file == caller_file) {
        graph.add_edge(call.caller, id);
        bound_local = true;
        break;  // at most one static of a given name per translation unit
      }
    }
    if (bound_local) continue;

    std::vector<NodeId> externals;
    for (const NodeId id : candidates) {
      if (graph.node(id).linkage == Linkage::External) externals.push_back(id);
    }
    for (const NodeId id : externals) graph.add_edge(call.caller, id);

    // More than one external definition of a name is a real C link error. We
    // cannot pick one, so we keep every edge and flag the ambiguity rather than
    // guessing (ADR-0005).
    if (externals.size() > 1) {
      graph.add_diagnostic(
          Diagnostic{call.callee, caller_file, call.line, std::move(externals)});
    }
  }
  return graph;
}

}  // namespace cartograph
