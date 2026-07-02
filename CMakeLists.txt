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

# ── libcartograph: the engine core ─────────────────────────────────────────
add_library(cartograph_core src/parser.cpp)
add_library(cartograph::core ALIAS cartograph_core)
target_include_directories(cartograph_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
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
