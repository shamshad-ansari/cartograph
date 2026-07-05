// The string arena underpins the struct-of-arrays graph (issue 0013): node
// names and file paths are stored once as bytes in a single buffer and referred
// to by an integer offset+length (StringRef), never as a per-node heap string.
#include "cartograph/string_arena.hpp"

#include <string>

#include <gtest/gtest.h>

namespace {

using cartograph::StringArena;
using cartograph::StringRef;

TEST(StringArena, RoundTripsInternedStrings) {
  StringArena arena;
  const StringRef a = arena.intern("caller_one");
  const StringRef b = arena.intern("src/main.c");

  EXPECT_EQ(arena.view(a), "caller_one");
  EXPECT_EQ(arena.view(b), "src/main.c");
}

TEST(StringArena, DedupesIdenticalStrings) {
  StringArena arena;
  const StringRef first = arena.intern("src/main.c");
  const std::size_t after_first = arena.size_bytes();
  const StringRef again = arena.intern("src/main.c");

  // Same content interns to the same location, and no new bytes are appended.
  EXPECT_EQ(first.offset, again.offset);
  EXPECT_EQ(first.length, again.length);
  EXPECT_EQ(arena.size_bytes(), after_first);
}

TEST(StringArena, DistinctStringsGetDistinctRefs) {
  StringArena arena;
  const StringRef a = arena.intern("alpha");
  const StringRef b = arena.intern("beta");

  EXPECT_NE(a.offset, b.offset);
  EXPECT_EQ(arena.view(a), "alpha");
  EXPECT_EQ(arena.view(b), "beta");
}

TEST(StringArena, ViewsStayValidAsArenaGrows) {
  // view() resolves against the current buffer, so a ref taken early must still
  // read correctly after many later interns have grown (and reallocated) it.
  StringArena arena;
  const StringRef first = arena.intern("the_first_symbol");
  for (int i = 0; i < 5000; ++i) {
    arena.intern("filler_symbol_" + std::to_string(i));
  }
  EXPECT_EQ(arena.view(first), "the_first_symbol");
}

TEST(StringArena, HandlesEmptyString) {
  StringArena arena;
  const StringRef empty = arena.intern("");
  EXPECT_EQ(empty.length, 0u);
  EXPECT_EQ(arena.view(empty), "");
}

}  // namespace
