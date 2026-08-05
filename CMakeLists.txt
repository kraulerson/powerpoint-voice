# 3.24 floor so the same tree configures with ubuntu-24.04's apt cmake (3.28)
# and local brew cmake (4.x). Qt's CMake API and FetchContent both predate this.
cmake_minimum_required(VERSION 3.24)

project(powerpoint_voice
  VERSION 0.1.0
  DESCRIPTION "Voice-controlled offline PowerPoint presentation controller"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standards & global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clang-tidy

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies
#
# Runtime UI framework: Qt 6 (ADR-0001). Widgets only for the MVP shell.
# Minimum 6.2 so the same tree configures on brew (6.9) and ubuntu apt (6.4).
# Additional deps (pugixml, libzip, Vosk, miniaudio, spdlog) are pulled in per
# feature during the Phase 2 Build Loop — added when the feature that needs them
# is built, not speculatively (CLAUDE.md dependency rule).
# ---------------------------------------------------------------------------
# Qt6::Test supplies QSignalSpy, used to assert the off-thread workers' signals
# (it is part of qtbase, so it adds no new package on brew or ubuntu apt).
find_package(Qt6 6.2 REQUIRED COMPONENTS Gui Widgets Test)
qt_standard_project_setup()

# OOXML parse layer (ADR-0001): libzip reads the .pptx package, pugixml parses
# the XML parts. Located via pkg-config (not each project's CMake config): on
# Ubuntu, libzip's installed CMake targets file asserts the libzip CLI tools
# (/usr/bin/zipcmp) exist, but they are not packaged — so find_package(libzip)
# hard-errors there. pkg-config finds the library + headers cleanly on both
# brew (macOS) and apt (Ubuntu) without dragging in the tools.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)
pkg_check_modules(PUGIXML REQUIRED IMPORTED_TARGET pugixml)

# Test framework: doctest (MIT), header-only, pinned by exact tag.
include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.4.11)
# doctest 2.4.11 ships cmake_minimum_required(VERSION 2.8), which CMake 4.x
# (local brew) rejects. This shim treats sub-3.5 minimums as 3.5. The variable
# is a no-op on CMake < 3.30 (ubuntu CI's 3.28), so it is safe on both.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
FetchContent_MakeAvailable(doctest)

# ---------------------------------------------------------------------------
# Vendored speech engine. Prepared (not modified) at build time — see the comment
# in cmake/VendorVosk.cmake for why the shipped file cannot be loaded as-is.
# ---------------------------------------------------------------------------
if(APPLE)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/VendorVosk.cmake")
  pptv_prepare_vosk(PPTV_VOSK_LIB)
endif()

# ---------------------------------------------------------------------------
# Library target (app logic — testable without the GUI event loop)
# ---------------------------------------------------------------------------
add_library(pptv_core STATIC
  src/core/app_info.cpp
  src/audio/audio_format.cpp
  src/loader/deck_loader.cpp
  src/render/slide_renderer.cpp
  src/command/number_parser.cpp
  src/command/command_matcher.cpp
  src/command/recognizer_controller.cpp
  src/present/notice.cpp
  src/present/presentation_controller.cpp
  src/present/display_geometry.cpp
  src/present/key_translator.cpp
  src/present/deck_load_worker.cpp
  src/present/pre_render_worker.cpp)
target_include_directories(pptv_core PUBLIC src)
# Qt6::Gui for the renderer (QImage/QPainter/QFont); libzip+pugixml for the loader.
target_link_libraries(pptv_core PUBLIC Qt6::Core Qt6::Gui
  PRIVATE PkgConfig::LIBZIP PkgConfig::PUGIXML)
target_compile_definitions(pptv_core PUBLIC
  POWERPOINT_VOICE_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# UI layer (widgets). Separate from pptv_core because core links Qt6::Gui only —
# keeping the decision layer free of Widgets is what lets the 169 core tests run
# under a plain QGuiApplication. All POLICY stays in core; these classes only paint
# and forward.
# ---------------------------------------------------------------------------
add_library(pptv_ui STATIC
  src/ui/start_view.cpp
  src/ui/slide_surface.cpp
  src/ui/notice_strip.cpp
  src/ui/presentation_window.cpp
  src/ui/quit_policy.cpp
  src/ui/app_shell.cpp)
target_link_libraries(pptv_ui PUBLIC pptv_core Qt6::Widgets)

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
qt_add_executable(powerpoint_voice
  src/main.cpp)
target_link_libraries(powerpoint_voice PRIVATE pptv_ui)
set_target_properties(powerpoint_voice PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON
  # The generated Info.plist MUST carry NSMicrophoneUsageDescription: macOS kills a
  # process outright when it opens an audio input device without one. CMake's default
  # template has no such key, so voice would have crashed the app on the presenter's
  # MacBook Pro and nowhere else — the dev Mac mini has no microphone to trigger it.
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "powerpoint-voice"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.kraulerson.powerpoint-voice"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# Headless dev/UAT utility: render a deck's slides to PNGs for fidelity checks.
add_executable(render_preview tools/render_preview.cpp)
target_link_libraries(render_preview PRIVATE pptv_core Qt6::Gui)

# Headless dev/UAT utility: map typed phrases to voice commands (F2/F3/F4).
add_executable(command_probe tools/command_probe.cpp)
target_link_libraries(command_probe PRIVATE pptv_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
