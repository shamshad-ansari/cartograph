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
};

// Extract function-definition facts from an already-parsed C source `tree`.
// `source` must be the exact buffer the tree was parsed from; it is used to read
// captured identifier text. Returns an empty vector for an empty tree.
std::vector<DefinitionFact> extract_definitions(const Tree& tree,
                                                std::string_view source);

}  // namespace cartograph
