#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cartograph/graph.hpp"
#include "cartograph/indexer.hpp"
#include "cartograph/parser.hpp"

namespace {

int usage(std::ostream& out) {
  out << "usage: cartograph <command> [args]\n\n"
         "commands:\n"
         "  parse <file.c>                       parse a C file and print its "
         "syntax tree\n"
         "  find-definition <name> --dir <path>  print file:line of each "
         "definition of <name>\n"
         "  who-calls <name> --dir <path>        print file:line of each "
         "function that calls <name>\n";
  return 2;
}

// Parse the shared `<name> --dir <path>` argument shape used by the graph
// queries. On success fills `name`/`dir` and returns true; otherwise reports the
// problem and returns false.
bool parse_name_and_dir(const std::vector<std::string_view>& args,
                        std::string_view command, std::string_view& name,
                        std::string_view& dir) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--dir") {
      if (++i >= args.size()) {
        std::cerr << "cartograph: '--dir' expects a path\n";
        return false;
      }
      dir = args[i];
    } else if (name.empty()) {
      name = args[i];
    } else {
      std::cerr << "cartograph: unexpected argument '" << args[i] << "'\n";
      return false;
    }
  }
  if (name.empty() || dir.empty()) {
    std::cerr << "cartograph: '" << command
              << "' expects <name> --dir <path>\n";
    return false;
  }
  return true;
}

// Read an entire file into a string. Returns false if it cannot be opened.
bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out = buffer.str();
  return true;
}

int cmd_parse(std::string_view path) {
  std::string source;
  if (!read_file(std::string(path), source)) {
    std::cerr << "cartograph: cannot open '" << path << "'\n";
    return 1;
  }

  cartograph::Parser parser;
  cartograph::Tree tree = parser.parse(source);
  if (tree.empty()) {
    std::cerr << "cartograph: failed to parse '" << path << "'\n";
    return 1;
  }

  cartograph::dump_tree(tree, std::cout);
  return 0;
}

int cmd_find_definition(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "find-definition", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // Collect and sort by (file, line) so output is deterministic regardless of
  // directory iteration order.
  std::vector<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId id : graph.nodes_named(name)) {
    const cartograph::Node& node = graph.node(id);
    hits.emplace_back(node.file, node.line);
  }
  std::sort(hits.begin(), hits.end());

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

int cmd_who_calls(const std::vector<std::string_view>& args) {
  std::string_view name;
  std::string_view dir;
  if (!parse_name_and_dir(args, "who-calls", name, dir)) {
    return usage(std::cerr);
  }

  const std::filesystem::path root(dir);
  if (!std::filesystem::is_directory(root)) {
    std::cerr << "cartograph: not a directory: '" << dir << "'\n";
    return 1;
  }

  const cartograph::Graph graph = cartograph::index_directory(root);

  // A caller is listed once even if it calls the target repeatedly or the name
  // has several definitions; std::set both dedupes and sorts by (file, line).
  std::set<std::pair<std::string, std::uint32_t>> hits;
  for (const cartograph::NodeId callee : graph.nodes_named(name)) {
    for (const cartograph::NodeId caller : graph.callers_of(callee)) {
      const cartograph::Node& node = graph.node(caller);
      hits.emplace(node.file, node.line);
    }
  }

  for (const auto& [file, line] : hits) {
    std::cout << file << ':' << line << '\n';
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage(std::cerr);

  const std::string_view command = argv[1];
  if (command == "parse") {
    if (argc != 3) {
      std::cerr << "cartograph: 'parse' expects exactly one file argument\n";
      return usage(std::cerr);
    }
    return cmd_parse(argv[2]);
  }
  if (command == "find-definition") {
    return cmd_find_definition(
        std::vector<std::string_view>(argv + 2, argv + argc));
  }
  if (command == "who-calls") {
    return cmd_who_calls(std::vector<std::string_view>(argv + 2, argv + argc));
  }

  std::cerr << "cartograph: unknown command '" << command << "'\n";
  return usage(std::cerr);
}
