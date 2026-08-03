# doctest test runner for the core library. Kept GUI-free so tests run headless
# in CI without a display server. Feature tests are added here first (RED) in
# each Build Loop, ahead of implementation.
add_executable(pptv_tests
  test_main.cpp
  test_app_info.cpp)
target_link_libraries(pptv_tests PRIVATE pptv_core doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pptv_tests)
