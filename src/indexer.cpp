#include "cartograph/indexer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

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
    for (DefinitionFact& fact : extract_definitions(tree, source)) {
      graph.add_node(Node{NodeKind::Function, std::move(fact.name),
                          path.string(), fact.line});
    }
  }
  return graph;
}

}  // namespace cartograph
