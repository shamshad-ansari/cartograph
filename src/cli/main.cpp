#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "cartograph/parser.hpp"

namespace {

int usage(std::ostream& out) {
  out << "usage: cartograph <command> [args]\n\n"
         "commands:\n"
         "  parse <file.c>   parse a C file and print its syntax tree\n";
  return 2;
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

  std::cerr << "cartograph: unknown command '" << command << "'\n";
  return usage(std::cerr);
}
