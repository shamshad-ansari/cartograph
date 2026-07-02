# Third-party dependencies, fetched and pinned in one place.
#
# Pinned versions (single source of truth — see acceptance criteria for 0001).
# Bump a tag here and nowhere else.
set(CARTOGRAPH_TREE_SITTER_TAG   v0.26.10 CACHE STRING "tree-sitter runtime version")
set(CARTOGRAPH_TREE_SITTER_C_TAG v0.24.2  CACHE STRING "tree-sitter-c grammar version")
set(CARTOGRAPH_GOOGLETEST_TAG    v1.17.0  CACHE STRING "GoogleTest version")
set(CARTOGRAPH_NLOHMANN_JSON_TAG v3.12.0  CACHE STRING "nlohmann/json version")

include(FetchContent)

# tree-sitter and tree-sitter-c are declared with SOURCE_SUBDIR pointing at a
# directory that has no CMakeLists.txt. That makes FetchContent download the
# sources without running their (optional, occasionally fragile) upstream CMake:
# we compile the runtime amalgamation and the generated grammar ourselves below.
# This is the deterministic, dependency-free build the acceptance criteria want,
# and matches the documented fallback of vendoring the generated parser.
FetchContent_Declare(treesitter_core
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG        ${CARTOGRAPH_TREE_SITTER_TAG}
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cartograph-no-configure)

FetchContent_Declare(treesitter_c
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c.git
  GIT_TAG        ${CARTOGRAPH_TREE_SITTER_C_TAG}
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  cartograph-no-configure)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        ${CARTOGRAPH_GOOGLETEST_TAG}
  GIT_SHALLOW    TRUE)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        ${CARTOGRAPH_NLOHMANN_JSON_TAG}
  GIT_SHALLOW    TRUE)

# Keep third-party test targets out of our build/test tree.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
# Windows-only, harmless elsewhere: link gtest against the shared CRT.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(treesitter_core treesitter_c googletest nlohmann_json)

# ── tree-sitter runtime ────────────────────────────────────────────────────
# lib/src/lib.c is the single-file amalgamation of the C runtime.
add_library(tree-sitter-runtime STATIC ${treesitter_core_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree-sitter-runtime
  PUBLIC  ${treesitter_core_SOURCE_DIR}/lib/include
  PRIVATE ${treesitter_core_SOURCE_DIR}/lib/src)
set_target_properties(tree-sitter-runtime PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# ── tree-sitter-c grammar ──────────────────────────────────────────────────
# The C grammar has no external scanner, so parser.c is the only source.
add_library(tree-sitter-c STATIC ${treesitter_c_SOURCE_DIR}/src/parser.c)
target_include_directories(tree-sitter-c PUBLIC ${treesitter_c_SOURCE_DIR}/src)
target_link_libraries(tree-sitter-c PUBLIC tree-sitter-runtime)
set_target_properties(tree-sitter-c PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
