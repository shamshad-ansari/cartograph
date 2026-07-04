#include "cartograph/extractor.hpp"

#include <tree_sitter/api.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "cartograph/embedded_queries.hpp"
#include "cartograph/parser.hpp"

// Provided by the compiled tree-sitter-c grammar (see cmake/Dependencies.cmake).
extern "C" const TSLanguage* tree_sitter_c(void);

namespace cartograph {
namespace {

// RAII owners for the tree-sitter query and cursor, so the extraction body can
// bail (e.g. throw) without leaking either handle.
struct Query {
  TSQuery* ptr;
  explicit Query(std::string_view src) {
    std::uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    ptr = ts_query_new(tree_sitter_c(), src.data(),
                       static_cast<std::uint32_t>(src.size()), &error_offset,
                       &error_type);
    if (ptr == nullptr) {
      throw std::runtime_error("invalid tree-sitter query for C definitions");
    }
  }
  ~Query() { ts_query_delete(ptr); }
  Query(const Query&) = delete;
  Query& operator=(const Query&) = delete;
};

struct Cursor {
  TSQueryCursor* ptr = ts_query_cursor_new();
  Cursor() = default;
  ~Cursor() { ts_query_cursor_delete(ptr); }
  Cursor(const Cursor&) = delete;
  Cursor& operator=(const Cursor&) = delete;
};

// Copy a node's source text out of `source` via its byte range.
std::string node_text(TSNode node, std::string_view source) {
  const std::uint32_t start = ts_node_start_byte(node);
  const std::uint32_t end = ts_node_end_byte(node);
  return std::string(source.substr(start, end - start));
}

TSNode field(TSNode node, const char* name) {
  return ts_node_child_by_field_name(node, name,
                                     static_cast<std::uint32_t>(strlen(name)));
}

// The identifier at the base of a function_definition's declarator chain — the
// function's name — or a null node if the declarator shape isn't recognized.
// Mirrors queries/c/definitions.scm (a function, optionally pointer-returning).
TSNode function_name_node(TSNode fn_def) {
  TSNode d = field(fn_def, "declarator");
  while (!ts_node_is_null(d) &&
         std::string_view(ts_node_type(d)) == "pointer_declarator") {
    d = field(d, "declarator");
  }
  if (ts_node_is_null(d) ||
      std::string_view(ts_node_type(d)) != "function_declarator") {
    return TSNode{};
  }
  return field(d, "declarator");
}

// Whether `fn_def` carries a `static` storage-class specifier — the mark of
// internal linkage. Storage-class specifiers appear as direct children of the
// function_definition (see the tree-sitter-c grammar), so a single-level scan
// suffices; their node text is the keyword itself.
bool has_static_storage(TSNode fn_def, std::string_view source) {
  const std::uint32_t children = ts_node_child_count(fn_def);
  for (std::uint32_t i = 0; i < children; ++i) {
    const TSNode child = ts_node_child(fn_def, i);
    if (std::string_view(ts_node_type(child)) == "storage_class_specifier" &&
        node_text(child, source) == "static") {
      return true;
    }
  }
  return false;
}

// The nearest enclosing function_definition of `node`, or a null node if the
// call is not inside one.
TSNode enclosing_function(TSNode node) {
  for (TSNode cur = ts_node_parent(node); !ts_node_is_null(cur);
       cur = ts_node_parent(cur)) {
    if (std::string_view(ts_node_type(cur)) == "function_definition") {
      return cur;
    }
  }
  return TSNode{};
}

// Run `query_src` against `tree` and collect every node bound to the capture
// named `capture` (e.g. "name", "path"), in match order. Extraction of the
// single-node fact kinds reduces to "find the nodes this pattern selects under
// this capture"; the query does the structural work. Captures are matched by
// name (rather than a fixed index) so the walk stays correct as patterns are
// added or reordered in the .scm file.
std::vector<TSNode> capture_nodes(const Tree& tree, std::string_view query_src,
                                  std::string_view capture) {
  std::vector<TSNode> nodes;
  Query query(query_src);
  Cursor cursor;
  ts_query_cursor_exec(cursor.ptr, query.ptr, ts_tree_root_node(tree.raw()));

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor.ptr, &match)) {
    for (std::uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture cap = match.captures[i];

      std::uint32_t name_len = 0;
      const char* capture_name =
          ts_query_capture_name_for_id(query.ptr, cap.index, &name_len);
      if (std::string_view(capture_name, name_len) != capture) continue;
      nodes.push_back(cap.node);
    }
  }
  return nodes;
}

}  // namespace

std::vector<DefinitionFact> extract_definitions(const Tree& tree,
                                                std::string_view source) {
  std::vector<DefinitionFact> facts;
  if (tree.empty()) return facts;

  for (const TSNode node : capture_nodes(tree, kCDefinitionsQuery, "name")) {
    DefinitionFact fact;
    fact.name = node_text(node, source);
    fact.line = ts_node_start_point(node).row + 1;
    // The function_definition owns the storage class; climb from the name.
    const TSNode fn = enclosing_function(node);
    fact.is_static = !ts_node_is_null(fn) && has_static_storage(fn, source);
    facts.push_back(std::move(fact));
  }
  return facts;
}

std::vector<DeclarationFact> extract_declarations(const Tree& tree,
                                                  std::string_view source) {
  std::vector<DeclarationFact> facts;
  if (tree.empty()) return facts;

  for (const TSNode node : capture_nodes(tree, kCDeclarationsQuery, "name")) {
    DeclarationFact fact;
    fact.name = node_text(node, source);
    fact.line = ts_node_start_point(node).row + 1;
    facts.push_back(std::move(fact));
  }
  return facts;
}

std::vector<CallFact> extract_calls(const Tree& tree, std::string_view source) {
  std::vector<CallFact> facts;
  if (tree.empty()) return facts;

  // The query finds the call sites; for each we climb to the enclosing function
  // to name the caller. Doing this in code rather than in the query keeps the
  // pattern free of a fixed nesting depth, so calls buried in control flow are
  // still attributed.
  Query query(kCCallsQuery);
  Cursor cursor;
  ts_query_cursor_exec(cursor.ptr, query.ptr, ts_tree_root_node(tree.raw()));

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor.ptr, &match)) {
    for (std::uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture capture = match.captures[i];

      std::uint32_t name_len = 0;
      const char* capture_name =
          ts_query_capture_name_for_id(query.ptr, capture.index, &name_len);
      if (std::string_view(capture_name, name_len) != "callee") continue;

      const TSNode fn = enclosing_function(capture.node);
      if (ts_node_is_null(fn)) continue;  // call outside any function body
      const TSNode caller = function_name_node(fn);
      if (ts_node_is_null(caller)) continue;  // unrecognized declarator shape

      CallFact fact;
      fact.caller = node_text(caller, source);
      fact.callee = node_text(capture.node, source);
      fact.line = ts_node_start_point(capture.node).row + 1;
      facts.push_back(std::move(fact));
    }
  }
  return facts;
}

std::vector<IncludeFact> extract_includes(const Tree& tree,
                                          std::string_view source) {
  std::vector<IncludeFact> facts;
  if (tree.empty()) return facts;

  for (const TSNode node : capture_nodes(tree, kCIncludesQuery, "path")) {
    // The captured token carries its delimiters — `"util.h"` or `<stdio.h>`. A
    // system_lib_string is the `<...>` form; both delimiter styles are a single
    // char on each side, so stripping the ends recovers the bare path.
    const bool is_system =
        std::string_view(ts_node_type(node)) == "system_lib_string";
    std::string text = node_text(node, source);
    if (text.size() >= 2) text = text.substr(1, text.size() - 2);

    IncludeFact fact;
    fact.target = std::move(text);
    fact.is_system = is_system;
    fact.line = ts_node_start_point(node).row + 1;
    facts.push_back(std::move(fact));
  }
  return facts;
}

}  // namespace cartograph
