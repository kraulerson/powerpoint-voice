#!/usr/bin/env bash
# Project test command (wired into .claude/test-command so the framework's
# commit-time gate can run the suite). Configures + incrementally builds + runs
# ctest. Local macOS dev env; CI uses its own workflow steps.
set -euo pipefail

cd "$(dirname "$0")/.."

# LLVM (clang-format/tidy) and the Qt/libzip/pugixml pkg-config paths on a brew
# machine. Guarded so this is a no-op where brew is absent.
if command -v brew >/dev/null 2>&1; then
  export PATH="$(brew --prefix llvm)/bin:$PATH"
  export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
  export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
fi

# Format check — mirrors CI's clang-format gate so a formatting-only violation
# fails HERE (at commit time) instead of only in CI. Fast, so run it first.
if command -v clang-format >/dev/null 2>&1; then
  git ls-files '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx' | xargs -r clang-format --dry-run --Werror
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build
ctest --test-dir build --output-on-failure
