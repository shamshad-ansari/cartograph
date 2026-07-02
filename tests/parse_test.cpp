#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "cartograph/parser.hpp"

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

}  // namespace

TEST(ParseSmoke, ParsesFixtureToNonEmptyTranslationUnit) {
  const std::string source = read_file(std::string(FIXTURE_DIR) + "/simple.c");
  ASSERT_FALSE(source.empty()) << "fixture simple.c should be readable";

  cartograph::Parser parser;
  cartograph::Tree tree = parser.parse(source);

  ASSERT_FALSE(tree.empty());
  EXPECT_EQ(tree.root_kind(), "translation_unit");
}

TEST(ParseSmoke, DumpsRecognizableStructure) {
  const std::string source = read_file(std::string(FIXTURE_DIR) + "/simple.c");
  cartograph::Parser parser;
  cartograph::Tree tree = parser.parse(source);

  std::ostringstream out;
  cartograph::dump_tree(tree, out);
  const std::string dump = out.str();

  EXPECT_NE(dump.find("translation_unit"), std::string::npos);
  EXPECT_NE(dump.find("function_definition"), std::string::npos);
}
