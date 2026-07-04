#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cartograph {

// Nodes are addressed by a dense integer id into a flat array, never by pointer
// (ADR-0007): this is cache-friendly for the traversal-heavy queries added in
// later slices and position-independent, a prerequisite for mmap persistence.
using NodeId = std::uint32_t;

// The kind of program entity a node represents. A `Function` is a definition
// (a body); a `FunctionDecl` is a prototype — a declaration without a body,
// typically in a header — linked to its defining `Function` when one is indexed.
// A `File` is an indexed source or header file, the endpoint of INCLUDES edges.
// `Struct`, `Union`, `Enum`, and `Typedef` are user-defined types — the tag of
// an aggregate definition or a typedef alias — and the endpoint of USES_TYPE
// edges from the functions that reference them.
enum class NodeKind {
  Function,
  FunctionDecl,
  File,
  Struct,
  Union,
  Enum,
  Typedef,
};

// Whether `kind` is one of the user-defined type nodes (the USES_TYPE target
// kinds), as opposed to a function, declaration, or file node.
inline bool is_type_node(NodeKind kind) {
  return kind == NodeKind::Struct || kind == NodeKind::Union ||
         kind == NodeKind::Enum || kind == NodeKind::Typedef;
}

// A definition's C linkage — the property that decides which calls can see it.
// `Internal` is a `static` definition, visible only within its own file;
// `External` (the default) is visible across translation units. Call resolution
// uses this to pick a target (ADR-0005).
enum class Linkage {
  External,
  Internal,
};

// One entity in the code graph. For this slice: a function definition and where
// it was found.
struct Node {
  NodeKind kind;
  std::string name;
  std::string file;         // path as indexed
  std::uint32_t line;       // 1-based line of the definition's name
  Linkage linkage;          // internal (static) vs external, for resolution
  std::uint64_t hash = 0;   // content hash of the file (File nodes only; else 0)
};

// A node reached by walking CALLS edges in reverse from a query target, paired
// with its shortest distance in edges to that target: `depth` 1 is a direct
// caller, 2 a caller's caller, and so on. The distance is what makes a
// blast-radius result readable — the nearest callers are the first to break.
struct Caller {
  NodeId node;
  std::uint32_t depth;
};

// An `#include` that did not resolve to a File node in the indexed set — a
// system header (`<stdio.h>`), or a local include whose target lies outside the
// indexed directory. Recorded for completeness so the include graph is honest
// about a file's external dependencies; it forms no INCLUDES edge, since there
// is no indexed file to link to.
struct UnresolvedInclude {
  NodeId includer;      // the File node carrying the directive
  std::string target;   // the path as written (delimiters stripped)
  bool is_system;       // `<...>` form vs an unresolved local `"..."`
  std::uint32_t line;   // 1-based line of the #include
};

// A file that was discovered during the crawl but not indexed — it could not be
// read, or tree-sitter found a syntax error in it. Recorded so a run over a real
// repository is honest about what it skipped instead of silently dropping files.
struct SkippedFile {
  std::string path;     // the file that was skipped
  std::string reason;   // why: "unreadable" or "syntax error"
};

// A resolution that C's linkage rules cannot make unambiguous, surfaced rather
// than silently guessed. Today the sole case is a call to a name with more than
// one external-linkage definition — a real-C link error — which is linked to
// all candidates and flagged here (ADR-0005).
struct Diagnostic {
  std::string callee;              // the ambiguous name at the call site
  std::string caller_file;         // file containing the call
  std::uint32_t caller_line;       // 1-based line of the call site
  std::vector<NodeId> candidates;  // every definition the call was linked to
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

  // Total number of edges of every kind — CALLS, INCLUDES, USES_TYPE, and
  // DECLARES — for the index summary. INCLUDES is counted once per edge, not
  // once per direction.
  std::size_t edge_count() const;

  // NodeIds whose name equals `name`, in insertion order; empty if none.
  const std::vector<NodeId>& nodes_named(std::string_view name) const;

  // Record a CALLS edge: `caller` calls `callee`. CALLS is the only edge kind
  // this slice has; the edge is indexed for reverse ("who calls?") lookup.
  void add_edge(NodeId caller, NodeId callee);

  // NodeIds with a CALLS edge into `id` — its callers — in insertion order;
  // empty if none.
  const std::vector<NodeId>& callers_of(NodeId id) const;

  // Reverse transitive closure over CALLS from every node in `seeds`: all direct
  // and indirect callers — the blast radius of changing those nodes. The walk is
  // breadth-first, so each returned `Caller.depth` is its shortest hop count from
  // a seed. A visited set makes recursion and mutual-recursion cycles terminate,
  // and the seeds themselves — the entity being changed, not something it affects
  // — are never in the result even when a cycle calls back into one.
  std::vector<Caller> transitive_callers(const std::vector<NodeId>& seeds) const;

  // Record an INCLUDES edge: file `includer` includes file `includee` (both File
  // nodes). Indexed for traversal in either direction — a file's includees and
  // its includers.
  void add_include(NodeId includer, NodeId includee);

  // File nodes that `id` directly includes — its includees — in insertion order;
  // empty if none.
  const std::vector<NodeId>& includes_of(NodeId id) const;

  // File nodes that directly include `id` — its includers — in insertion order;
  // empty if none.
  const std::vector<NodeId>& included_by(NodeId id) const;

  // Record an `#include` that resolved to no indexed file (system or external).
  void add_unresolved_include(UnresolvedInclude include);

  // Every unresolved include collected while building the graph, in source order.
  const std::vector<UnresolvedInclude>& unresolved_includes() const {
    return unresolved_includes_;
  }

  // Record a USES_TYPE edge: function `user` references type `type` (a Struct,
  // Union, Enum, or Typedef node). Indexed for reverse ("who uses this type?")
  // lookup.
  void add_uses_type(NodeId user, NodeId type);

  // Function nodes with a USES_TYPE edge into `id` — the functions that
  // reference the type — in insertion order; empty if none.
  const std::vector<NodeId>& users_of(NodeId id) const;

  // Record a DECLARES link: the declaration `decl` (a FunctionDecl) declares the
  // definition `def` (a Function). A declaration has at most one definition in
  // the indexed set, so a later call replaces any earlier one.
  void link_declaration(NodeId decl, NodeId def);

  // The definition that declaration `decl` was linked to, or nullopt when no
  // matching definition was found in the indexed set.
  std::optional<NodeId> definition_of(NodeId decl) const;

  // Record an ambiguous/erroneous resolution surfaced during indexing.
  void add_diagnostic(Diagnostic diagnostic);

  // Every diagnostic collected while building the graph, in the order found.
  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  // Record a file that the crawl discovered but did not index.
  void add_skipped_file(SkippedFile skipped);

  // Every skipped file collected while crawling, in discovery order.
  const std::vector<SkippedFile>& skipped_files() const { return skipped_files_; }

 private:
  std::vector<Node> nodes_;
  std::unordered_map<std::string, std::vector<NodeId>> by_name_;
  std::unordered_map<NodeId, std::vector<NodeId>> callers_by_callee_;
  std::unordered_map<NodeId, std::vector<NodeId>> includees_by_file_;
  std::unordered_map<NodeId, std::vector<NodeId>> includers_by_file_;
  std::unordered_map<NodeId, std::vector<NodeId>> users_by_type_;
  std::unordered_map<NodeId, NodeId> definition_by_decl_;
  std::vector<UnresolvedInclude> unresolved_includes_;
  std::vector<Diagnostic> diagnostics_;
  std::vector<SkippedFile> skipped_files_;
};

}  // namespace cartograph
