# doctest test runner for the core library. Kept GUI-free so tests run headless
# in CI without a display server. Feature tests are added here first (RED) in
# each Build Loop, ahead of implementation.
add_executable(pptv_tests
  test_main.cpp
  test_app_info.cpp
  test_deck_loader.cpp)
target_link_libraries(pptv_tests PRIVATE pptv_core doctest::doctest)
target_compile_definitions(pptv_tests PRIVATE
  FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pptv_tests)
