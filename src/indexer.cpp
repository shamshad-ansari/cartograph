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

  // FunctionDecl nodes to link once every definition is known, for the same
  // reason: a prototype's defining function may live in a file indexed later.
  std::vector<NodeId> pending_decls;

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

    // Header prototypes become FunctionDecl nodes, distinct from definitions.
    // Their linkage field is unused (prototypes carry no storage of their own);
    // External is the neutral default and keeps them out of static shadowing.
    for (DeclarationFact& decl : extract_declarations(tree, source)) {
      const NodeId id = graph.add_node(Node{NodeKind::FunctionDecl, decl.name,
                                            path.string(), decl.line,
                                            Linkage::External});
      pending_decls.push_back(id);
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
      if (def.kind != NodeKind::Function) continue;  // a call binds to a body
      if (def.linkage == Linkage::Internal && def.file == caller_file) {
        graph.add_edge(call.caller, id);
        bound_local = true;
        break;  // at most one static of a given name per translation unit
      }
    }
    if (bound_local) continue;

    std::vector<NodeId> externals;
    for (const NodeId id : candidates) {
      const Node& def = graph.node(id);
      if (def.kind == NodeKind::Function && def.linkage == Linkage::External) {
        externals.push_back(id);
      }
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

  // Third pass — link each declaration to the function it declares. A prototype
  // declares an external-linkage function, so we bind it to the unique external
  // definition of its name; failing that, to the sole definition of any linkage.
  // Zero definitions (a header-only prototype) or an ambiguous set leaves the
  // declaration unlinked rather than guessed at.
  for (const NodeId decl : pending_decls) {
    const std::vector<NodeId>& candidates =
        graph.nodes_named(graph.node(decl).name);

    std::vector<NodeId> externals;
    std::vector<NodeId> definitions;
    for (const NodeId id : candidates) {
      const Node& def = graph.node(id);
      if (def.kind != NodeKind::Function) continue;
      definitions.push_back(id);
      if (def.linkage == Linkage::External) externals.push_back(id);
    }

    if (externals.size() == 1) {
      graph.link_declaration(decl, externals.front());
    } else if (externals.empty() && definitions.size() == 1) {
      graph.link_declaration(decl, definitions.front());
    }
  }
  return graph;
}

}  // namespace cartograph
