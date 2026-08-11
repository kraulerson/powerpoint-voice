# Windows test build — FIRST PLATFORM PORT, in progress.
#
# Mirrors scripts/make-test-build.sh, which does the same job for macOS. It exists
# as a script rather than inline workflow steps for two reasons: the macOS build is
# a script and the platforms should look alike, and inline PowerShell breaks
# Semgrep's parse of the workflow (it reads every `run:` block as bash, where a
# backtick is command substitution rather than a line continuation). A file the
# SAST scanner cannot parse cannot be vouched for, so the workflow stays pure YAML.
#
# STATUS: this platform has never produced a working binary. Blockers are being
# peeled one CI run at a time, deliberately — each run buys the NEXT real
# diagnostic instead of a guess about it.
#
#   [done]  CMake configure reached MSVC, Qt and threads
#   [done]  find_package(PkgConfig) — supplied via vcpkg's pkgconf
#   [open]  no libvosk.dll is vendored AT ALL. third_party/vosk/lib holds only
#           linux-x86_64 and macos-universal2. Vosk 0.3.44 is pinned because it is
#           the last version with a macOS universal2 build and has NO Windows
#           wheel; 0.3.45 has Windows but no macOS. Windows would therefore run a
#           DIFFERENT ENGINE VERSION from the platform that ships — a decision for
#           the Orchestrator, not a detail to settle in a build script.
#   [open]  src/ui/app_shell.cpp includes <unistd.h> and calls ::_exit(0) (POSIX).
#           MSVC needs <process.h> and _exit.
#   [open]  no packaging: windeployqt, no installer, no signing.
#   [open]  the microphone permission model is entirely different and unhandled.
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

$vcpkg = $env:VCPKG_INSTALLATION_ROOT
$qtDir = $args[0]
if (-not $qtDir) { throw "usage: build-windows.ps1 <qt-prefix-path>" }

Write-Host "==> vcpkg dependencies"
& vcpkg install libzip:x64-windows pugixml:x64-windows pkgconf:x64-windows

# CMakeLists.txt locates libzip and pugixml through pkg-config, chosen because
# libzip's installed CMake targets file hard-errors on Ubuntu (it asserts CLI tools
# apt does not package). Correct for both Unix platforms; simply absent on Windows.
$env:PKG_CONFIG_PATH = Join-Path $vcpkg "installed\x64-windows\lib\pkgconfig"
$env:PATH = (Join-Path $vcpkg "installed\x64-windows\tools\pkgconf") + ";" + $env:PATH

Write-Host "==> configure"
$toolchain = Join-Path $vcpkg "scripts\buildsystems\vcpkg.cmake"
& cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_PREFIX_PATH="$qtDir" -DCMAKE_BUILD_TYPE=Release

Write-Host "==> build"
& cmake --build build-win --config Release
