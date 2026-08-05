# doctest test runner for the core library. Kept GUI-free so tests run headless
# in CI without a display server. Feature tests are added here first (RED) in
# each Build Loop, ahead of implementation.
add_executable(pptv_tests
  test_main.cpp
  test_app_info.cpp
  test_audio_format.cpp
  test_audio_capture.cpp
  test_vosk_recognizer.cpp
  test_vosk_engine.cpp
  test_voice_pipeline.cpp
  test_deck_loader.cpp
  test_slide_renderer.cpp
  test_number_parser.cpp
  test_command_matcher.cpp
  test_recognizer_controller.cpp
  test_presentation_controller.cpp
  test_display_geometry.cpp
  test_key_translator.cpp
  test_deck_load_worker.cpp
  test_pre_render_worker.cpp)
target_link_libraries(pptv_tests PRIVATE pptv_core doctest::doctest Qt6::Test)
target_compile_definitions(pptv_tests PRIVATE
  FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  PPTV_VOSK_MODEL_DIR="${CMAKE_BINARY_DIR}/vosk/model/vosk-model-small-en-us-0.15")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pptv_tests)

# Widget tests (own QApplication main) — see tests/ui/CMakeLists.txt.
add_subdirectory(ui)
