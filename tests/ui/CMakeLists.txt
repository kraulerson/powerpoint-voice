# Widget tests live in their OWN binary because QWidget needs a QApplication, while
# the existing pptv_tests uses QGuiApplication. A second binary costs a few lines and
# leaves the 169 existing tests completely untouched.
add_executable(pptv_ui_tests
  ui_test_main.cpp
  test_widgets.cpp)
target_link_libraries(pptv_ui_tests PRIVATE pptv_ui doctest::doctest Qt6::Test Qt6::Widgets)
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pptv_ui_tests)
