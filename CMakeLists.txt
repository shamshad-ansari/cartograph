cmake_minimum_required(VERSION 3.24)
project(cartograph LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so editor tooling (clangd / VS Code C/C++) sees the
# real compile flags: include paths, C++20 mode, and defines like FIXTURE_DIR.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)

# ── embedded query patterns ────────────────────────────────────────────────
# Query patterns are authored as real .scm files (a clean seam for adding
# languages and fact kinds) but compiled into the binary as string literals, so
# extraction never depends on query files being found relative to the working
# directory. Reconfigure is triggered when a pattern file changes.
set(CARTOGRAPH_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/definitions.scm
  ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/declarations.scm
  ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/calls.scm
  ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/includes.scm)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/definitions.scm
  CARTOGRAPH_C_DEFINITIONS_QUERY)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/declarations.scm
  CARTOGRAPH_C_DECLARATIONS_QUERY)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/calls.scm
  CARTOGRAPH_C_CALLS_QUERY)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/queries/c/includes.scm
  CARTOGRAPH_C_INCLUDES_QUERY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embedded_queries.hpp.in
  ${CARTOGRAPH_GENERATED_DIR}/cartograph/embedded_queries.hpp
  @ONLY)

# ── libcartograph: the engine core ─────────────────────────────────────────
add_library(cartograph_core
  src/parser.cpp
  src/graph.cpp
  src/extractor.cpp
  src/indexer.cpp)
add_library(cartograph::core ALIAS cartograph_core)
target_include_directories(cartograph_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CARTOGRAPH_GENERATED_DIR})
target_link_libraries(cartograph_core PUBLIC tree-sitter-c)
set_target_properties(cartograph_core PROPERTIES OUTPUT_NAME cartograph)

# ── cartograph: the CLI ────────────────────────────────────────────────────
add_executable(cartograph src/cli/main.cpp)
target_link_libraries(cartograph PRIVATE cartograph_core)

# ── tests ──────────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
