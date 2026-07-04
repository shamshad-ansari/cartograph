#pragma once

#include <ostream>
#include <string>
#include <string_view>

// Forward declarations of the opaque tree-sitter types, so callers of this
// header don't need tree-sitter on their include path. These match the
// typedefs in <tree_sitter/api.h>; redefining a typedef to the same type is
// well-formed in C++, so translation units may include both headers.
extern "C" {
typedef struct TSTree TSTree;
typedef struct TSParser TSParser;
}

namespace cartograph {

// RAII owner of a parsed tree-sitter syntax tree. Move-only.
class Tree {
public:
  explicit Tree(TSTree* tree) noexcept;
  ~Tree();

  Tree(Tree&&) noexcept;
  Tree& operator=(Tree&&) noexcept;
  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;

  // True when parsing produced no tree at all.
  bool empty() const noexcept { return tree_ == nullptr; }

  // True when the tree contains a syntax error — an ERROR or a missing node
  // anywhere in it. Tree-sitter is error-tolerant and still yields a tree for
  // malformed input, so this is how indexing decides a file is unparseable and
  // should be skipped. False for an empty tree (nothing was parsed).
  bool has_error() const noexcept;

  // The kind of the root node, e.g. "translation_unit" for a C file.
  std::string root_kind() const;

  // The underlying tree-sitter handle (non-owning).
  TSTree* raw() const noexcept { return tree_; }

private:
  TSTree* tree_;
};

// RAII owner of a tree-sitter parser configured for the C language. Move-only.
class Parser {
public:
  Parser();
  ~Parser();

  Parser(Parser&&) noexcept;
  Parser& operator=(Parser&&) noexcept;
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  // Parse a whole C source buffer into a syntax tree.
  Tree parse(std::string_view source) const;

private:
  TSParser* parser_;
};

// Write a readable, indented view of the tree's named nodes to `out`: each line
// carries the node kind, its byte range, and its 1-based line:column range.
void dump_tree(const Tree& tree, std::ostream& out);

}  // namespace cartograph
