#include "cartograph/extractor.hpp"

#include <tree_sitter/api.h>

#include <cstdint>
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

}  // namespace

std::vector<DefinitionFact> extract_definitions(const Tree& tree,
                                                std::string_view source) {
  std::vector<DefinitionFact> facts;
  if (tree.empty()) return facts;

  // The query does the structural matching; we only walk its results. Each
  // match corresponds to one function definition, and we pull the identifier
  // out of its "name" capture. Captures are matched by name (rather than a
  // fixed index) so the extractor stays correct as patterns are added or
  // reordered in the .scm file.
  Query query(kCDefinitionsQuery);
  Cursor cursor;
  ts_query_cursor_exec(cursor.ptr, query.ptr, ts_tree_root_node(tree.raw()));

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor.ptr, &match)) {
    for (std::uint16_t i = 0; i < match.capture_count; ++i) {
      const TSQueryCapture capture = match.captures[i];

      std::uint32_t name_len = 0;
      const char* capture_name =
          ts_query_capture_name_for_id(query.ptr, capture.index, &name_len);
      if (std::string_view(capture_name, name_len) != "name") continue;

      // Read the identifier's text straight out of the source buffer via the
      // captured node's byte range, and report its 1-based start line.
      const TSNode node = capture.node;
      const std::uint32_t start = ts_node_start_byte(node);
      const std::uint32_t end = ts_node_end_byte(node);

      DefinitionFact fact;
      fact.name = std::string(source.substr(start, end - start));
      fact.line = ts_node_start_point(node).row + 1;
      facts.push_back(std::move(fact));
    }
  }
  return facts;
}

}  // namespace cartograph
