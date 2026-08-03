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
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
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
# Library target (app logic — testable without the GUI event loop)
# ---------------------------------------------------------------------------
add_library(pptv_core STATIC
  src/core/app_info.cpp
  src/loader/deck_loader.cpp)
target_include_directories(pptv_core PUBLIC src)
target_link_libraries(pptv_core PUBLIC Qt6::Core PRIVATE PkgConfig::LIBZIP PkgConfig::PUGIXML)
target_compile_definitions(pptv_core PUBLIC
  POWERPOINT_VOICE_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# GUI application
# ---------------------------------------------------------------------------
qt_add_executable(powerpoint_voice
  src/main.cpp
  src/ui/start_view.cpp)
target_link_libraries(powerpoint_voice PRIVATE pptv_core Qt6::Widgets)
set_target_properties(powerpoint_voice PROPERTIES
  WIN32_EXECUTABLE ON
  MACOSX_BUNDLE ON)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
