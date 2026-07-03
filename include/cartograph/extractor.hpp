#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cartograph {

class Tree;

// A raw structural fact produced by matching tree-sitter query patterns against
// a parsed source file, before it is interned into the graph.
//
// Extraction is kept deliberately separate from graph construction and query
// resolution: supporting a new language or a new relationship is then mostly a
// matter of new .scm patterns plus a fact type, not changes to graph or query
// code.
struct DefinitionFact {
  std::string name;
  std::uint32_t line;  // 1-based line of the function's name
  bool is_static;      // has a `static` storage class -> internal linkage
};

// A call site: the enclosing function's name (`caller`) and the still-
// unresolved name it calls (`callee`). Resolution to definition node(s) happens
// later, in the indexer, once every file's definitions are known.
struct CallFact {
  std::string caller;
  std::string callee;
  std::uint32_t line;  // 1-based line of the call site
};

// Extract function-definition facts from an already-parsed C source `tree`.
// `source` must be the exact buffer the tree was parsed from; it is used to read
// captured identifier text. Returns an empty vector for an empty tree.
std::vector<DefinitionFact> extract_definitions(const Tree& tree,
                                                std::string_view source);

// Extract call-site facts from an already-parsed C source `tree`. Each call is
// attributed to the function_definition that encloses it; calls not inside a
// recognized function definition are dropped. `source` must be the buffer the
// tree was parsed from. Returns an empty vector for an empty tree.
std::vector<CallFact> extract_calls(const Tree& tree, std::string_view source);

}  // namespace cartograph
