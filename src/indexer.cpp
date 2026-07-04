
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

// 64-bit FNV-1a content hash. Deterministic and dependency-free — the property
// that matters for the incremental-reindex groundwork (issue 0016), where a
// file's stored hash is compared against a fresh one to decide if it changed.
std::uint64_t content_hash(std::string_view data) {
  std::uint64_t hash = 1469598103934665603ULL;  // FNV offset basis
  for (const unsigned char byte : data) {
    hash ^= byte;
    hash *= 1099511628211ULL;  // FNV prime
  }
  return hash;
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

  // Type references, resolved in a second pass: a function may reference a type
  // declared in a header indexed later, so we record the (already-known) using
  // function node with the still-unresolved type name and bind USES_TYPE edges
  // once every type node exists.
  struct PendingTypeUse {
    NodeId user;
    std::string type;
    std::uint32_t line;  // 1-based line of the reference, for future diagnostics
  };
  std::vector<PendingTypeUse> pending_type_uses;

  // Include directives, resolved in a second pass: an included file may be
  // indexed after the file that includes it, so we defer until every File node
  // exists. `file_by_path` maps each indexed file's normalized path to its File
  // node, the target of resolution.
  struct PendingInclude {
    NodeId includer;
    std::string target;
    bool is_system;
    std::uint32_t line;
  };
  std::vector<PendingInclude> pending_includes;
  std::unordered_map<std::string, NodeId> file_by_path;

  // Walk the whole tree under `dir`. The error-code overloads plus
  // skip_permission_denied keep the crawl from throwing on an unreadable
  // subdirectory, so one bad entry never aborts indexing of a real repository.
  std::error_code ec;
  std::filesystem::recursive_directory_iterator it(
      dir, std::filesystem::directory_options::skip_permission_denied, ec);
  const std::filesystem::recursive_directory_iterator end;
  for (; !ec && it != end; it.increment(ec)) {
    const std::filesystem::path& path = it->path();
    std::error_code stat_ec;
    if (!it->is_regular_file(stat_ec) || stat_ec || !is_c_source(path)) continue;

    std::string source;
    if (!read_file(path, source)) {
      graph.add_skipped_file(SkippedFile{path.string(), "unreadable"});
      continue;
    }

    const Tree tree = parser.parse(source);

    // A malformed file still yields a (partial) tree from the error-tolerant
    // parser; rather than index half-parsed garbage we skip it with a warning
    // and press on, so a single bad file doesn't poison the whole index.
    if (tree.has_error()) {
      graph.add_skipped_file(SkippedFile{path.string(), "syntax error"});
      continue;
    }

    // A File node per indexed file — an INCLUDES endpoint — keyed by normalized
    // path so an `#include` can resolve to it. The name is the basename, which
    // is how include-graph looks a file up; the full path lives in `file`.
    const NodeId file_id = graph.add_node(Node{NodeKind::File,
                                               path.filename().string(),
                                               path.string(), 0,
                                               Linkage::External,
                                               content_hash(source)});
    file_by_path.emplace(path.lexically_normal().string(), file_id);

    for (IncludeFact& inc : extract_includes(tree, source)) {
      pending_includes.push_back(
          {file_id, std::move(inc.target), inc.is_system, inc.line});
    }

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

    // User-defined types become typed nodes. Linkage is meaningless for a type;
    // External is the neutral default, matching the other non-function nodes.
    for (TypeFact& type : extract_types(tree, source)) {
      NodeKind kind = NodeKind::Struct;
      switch (type.category) {
        case TypeCategory::Struct:  kind = NodeKind::Struct;  break;
        case TypeCategory::Union:   kind = NodeKind::Union;   break;
        case TypeCategory::Enum:    kind = NodeKind::Enum;    break;
        case TypeCategory::Typedef: kind = NodeKind::Typedef; break;
      }
      graph.add_node(
          Node{kind, type.name, path.string(), type.line, Linkage::External});
    }

    // Type references, attributed to their enclosing function (already a node in
    // this file). The type name is resolved to type node(s) in a later pass.
    for (TypeUseFact& use : extract_type_uses(tree, source)) {
      const auto user = local_defs.find(use.function);
      if (user == local_defs.end()) continue;  // enclosing def not indexed
      pending_type_uses.push_back({user->second, std::move(use.type), use.line});
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

  // Fourth pass — resolve includes. A local `"..."` include is resolved like a C
  // compiler would: relative to the directory of the including file. A hit in
  // the indexed set becomes an INCLUDES edge; a miss — a system `<...>` header
  // or a target outside the indexed set — is recorded as unresolved, not linked,
  // and never errors (a project's external headers are simply not in the graph).
  for (const PendingInclude& inc : pending_includes) {
    if (!inc.is_system) {
      const std::filesystem::path includer_path = graph.node(inc.includer).file;
      const std::string candidate =
          (includer_path.parent_path() / inc.target).lexically_normal().string();
      const auto it = file_by_path.find(candidate);
      if (it != file_by_path.end()) {
        graph.add_include(inc.includer, it->second);
        continue;
      }
    }
    graph.add_unresolved_include(
        UnresolvedInclude{inc.includer, inc.target, inc.is_system, inc.line});
  }

  // Fifth pass — resolve type references. A referenced name binds to every type
  // node that carries it: usually one, but a tag and typedef sharing a name
  // (`typedef struct Point { ... } Point;`) are both legitimate targets. Names
  // with no type node — a built-in slipped through, or a type declared outside
  // the indexed set — bind to nothing and form no edge, never an error.
  for (const PendingTypeUse& use : pending_type_uses) {
    for (const NodeId id : graph.nodes_named(use.type)) {
      if (is_type_node(graph.node(id).kind)) graph.add_uses_type(use.user, id);
    }
  }
  return graph;
}

}  // namespace cartograph
