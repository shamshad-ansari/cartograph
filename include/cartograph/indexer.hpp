#pragma once

#include <filesystem>

#include "cartograph/graph.hpp"

namespace cartograph {

// Parse every .c/.h file directly inside `dir` (non-recursive) and record each
// function definition as a Function node in the returned graph. Files that
// cannot be read are skipped. Recursive crawling and per-file hashing arrive in
// issue 0009.
Graph index_directory(const std::filesystem::path& dir);

}  // namespace cartograph
