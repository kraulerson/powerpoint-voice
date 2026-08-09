#!/usr/bin/env bash
# Verifies that every doctest TEST_CASE in the built binaries is actually RUN by
# ctest — not merely registered.
#
# WHY THIS EXISTS
# ---------------
# doctest_discover_tests() puts each TEST_CASE name into a CMake list, and CMake's
# list separator is ';'. A test whose NAME contains a semicolon is therefore split:
# one registration per fragment. ctest then invokes the binary with
# `--test-case=<fragment>`, doctest matches nothing, runs 0 cases and exits 0 —
# and ctest prints "Passed".
#
# Measured on this repo at Phase 3: SEVEN tests had never once executed under
# ctest while the suite reported 100% green, including a regression test committed
# the same day for BUG-71. The only visible symptom was that ctest listed MORE
# tests than the binaries contain, which nobody had reason to check.
#
# Comparing the two name sets catches that, and any future truncation of any kind,
# rather than special-casing the one character known to break it today.
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build}"

if [ ! -d "$BUILD_DIR" ]; then
  echo "lint-test-names: no build directory at '$BUILD_DIR' — nothing to check." >&2
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# What ctest will actually invoke.
ctest --test-dir "$BUILD_DIR" -N 2>/dev/null | sed -n 's/^  Test *#[0-9]*: //p' | sort -u >"$tmp/ctest.txt"

# What the binaries actually contain.
: >"$tmp/doctest.txt"
found_binary=0
while IFS= read -r bin; do
  [ -x "$bin" ] || continue
  found_binary=1
  "$bin" --list-test-cases 2>/dev/null |
    grep -v '^\[doctest\]' | grep -v '^=*$' | grep -v '^[[:space:]]*$' >>"$tmp/doctest.txt"
done < <(find "$BUILD_DIR" -type f -perm -u+x -name '*tests' 2>/dev/null)

if [ "$found_binary" -eq 0 ]; then
  echo "lint-test-names: no test binaries found under '$BUILD_DIR' — nothing to check." >&2
  exit 0
fi
sort -u "$tmp/doctest.txt" -o "$tmp/doctest.txt"

status=0

# Tests the binaries have that ctest will never run. This is the failure that
# masquerades as a green suite.
if ! comm -23 "$tmp/doctest.txt" "$tmp/ctest.txt" | grep -q '^'; then
  : # none — good
else
  echo "ERROR: these TEST_CASEs exist in the binaries but ctest NEVER RUNS them:" >&2
  comm -23 "$tmp/doctest.txt" "$tmp/ctest.txt" | sed 's/^/    /' >&2
  echo "  Most likely cause: a ';' in the test name, which CMake treats as a list" >&2
  echo "  separator, so the registration is truncated and matches no test case." >&2
  status=1
fi

# The mirror image: names ctest will invoke that no test case answers to. Those
# report Passed while running nothing at all.
if comm -13 "$tmp/doctest.txt" "$tmp/ctest.txt" | grep -q '^'; then
  echo "ERROR: ctest will invoke these names, which match NO test case (0 run, exit 0):" >&2
  comm -13 "$tmp/doctest.txt" "$tmp/ctest.txt" | sed 's/^/    /' >&2
  status=1
fi

if [ "$status" -eq 0 ]; then
  echo "lint-test-names: $(wc -l <"$tmp/doctest.txt" | tr -d ' ') test cases, all runnable by ctest."
fi
exit "$status"
