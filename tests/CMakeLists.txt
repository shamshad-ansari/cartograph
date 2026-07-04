add_executable(cartograph_tests parse_test.cpp definition_test.cpp who_calls_test.cpp linkage_test.cpp declaration_test.cpp blast_radius_test.cpp)
target_link_libraries(cartograph_tests PRIVATE cartograph_core GTest::gtest_main)
target_compile_definitions(cartograph_tests
  PRIVATE FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(GoogleTest)
gtest_discover_tests(cartograph_tests)
