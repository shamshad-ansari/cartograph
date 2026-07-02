#include "cartograph/parser.hpp"

#include <tree_sitter/api.h>

#include <cstdint>
#include <stdexcept>

// Provided by the compiled tree-sitter-c grammar (see cmake/Dependencies.cmake).
extern "C" const TSLanguage* tree_sitter_c(void);

namespace cartograph {

Parser::Parser() : parser_(ts_parser_new()) {
  if (parser_ == nullptr) {
    throw std::runtime_error("failed to allocate tree-sitter parser");
  }
  if (!ts_parser_set_language(parser_, tree_sitter_c())) {
    ts_parser_delete(parser_);
    throw std::runtime_error(
        "failed to set tree-sitter-c language (ABI version mismatch)");
  }
}

Parser::~Parser() {
  if (parser_ != nullptr) ts_parser_delete(parser_);
}

Parser::Parser(Parser&& other) noexcept : parser_(other.parser_) {
  other.parser_ = nullptr;
}

Parser& Parser::operator=(Parser&& other) noexcept {
  if (this != &other) {
    if (parser_ != nullptr) ts_parser_delete(parser_);
    parser_ = other.parser_;
    other.parser_ = nullptr;
  }
  return *this;
}

Tree Parser::parse(std::string_view source) const {
  TSTree* tree = ts_parser_parse_string(
      parser_, nullptr, source.data(),
      static_cast<uint32_t>(source.size()));
  return Tree(tree);
}

Tree::Tree(TSTree* tree) noexcept : tree_(tree) {}

Tree::~Tree() {
  if (tree_ != nullptr) ts_tree_delete(tree_);
}

Tree::Tree(Tree&& other) noexcept : tree_(other.tree_) {
  other.tree_ = nullptr;
}

Tree& Tree::operator=(Tree&& other) noexcept {
  if (this != &other) {
    if (tree_ != nullptr) ts_tree_delete(tree_);
    tree_ = other.tree_;
    other.tree_ = nullptr;
  }
  return *this;
}

std::string Tree::root_kind() const {
  if (tree_ == nullptr) return {};
  return ts_node_type(ts_tree_root_node(tree_));
}

namespace {

void dump_node(TSNode node, std::ostream& out, int depth) {
  const TSPoint start = ts_node_start_point(node);
  const TSPoint end = ts_node_end_point(node);

  out << std::string(static_cast<std::size_t>(depth) * 2, ' ')
      << ts_node_type(node) << " ["
      << ts_node_start_byte(node) << '-' << ts_node_end_byte(node) << "] (L"
      << start.row + 1 << ':' << start.column + 1 << "-L"
      << end.row + 1 << ':' << end.column + 1 << ")\n";

  const uint32_t children = ts_node_named_child_count(node);
  for (uint32_t i = 0; i < children; ++i) {
    dump_node(ts_node_named_child(node, i), out, depth + 1);
  }
}

}  // namespace

void dump_tree(const Tree& tree, std::ostream& out) {
  if (tree.empty()) {
    out << "(empty tree)\n";
    return;
  }
  dump_node(ts_tree_root_node(tree.raw()), out, 0);
}

}  // namespace cartograph
