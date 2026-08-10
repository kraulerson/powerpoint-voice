#!/usr/bin/env bash
# Verifies that every doctest TEST_CASE in the built binaries is actually RUN by
# ctest — not merely registered.
#
# WHY THIS EXISTS
# ---------------
# doctest_discover_tests() writes each TEST_CASE name into a CMake list, and CMake's
# list separator is ';'. A test whose NAME contains a semicolon is registered as
# fragments; ctest then invokes the binary with `--test-case=<fragment>`, doctest
# matches nothing, runs 0 cases and exits 0, and ctest prints "Passed".
#
# Measured on this repo at Phase 3: SEVEN tests were in that state at the moment of
# discovery — five that had been dead since Phase 2, and two written the same day,
# one of which was already committed. Nothing was visible except that ctest listed
# more tests than the binaries contain, which nobody had reason to compare.
#
# HOW IT CHECKS (rewritten after an adversarial review, BUG-84)
# -------------
# The first version had three holes, all of them found by building a deliberately
# broken probe project:
#
#   1. FAIL-OPEN. It located test binaries with `find -name '*tests'` and exited 0
#      with a reassuring message when that matched nothing. A target named
#      `pptv_chaos_test` — singular — silently disabled the entire check, including
#      the required CI step. Binaries are now discovered from ctest's OWN command
#      lines, so there is no name convention to get wrong, and finding none is an
#      ERROR rather than a pass.
#   2. FALSE NEGATIVE on '\n'. doctestAddTests.cmake splits on newline AND ';'. The
#      old check compared name SETS read line-by-line, so it applied the identical
#      newline split to its own side and the two agreed. The authoritative check is
#      now a COUNT — doctest's own "unskipped test cases passing the current
#      filters: N" against the number of ctest entries that invoke that binary —
#      which no separator can disguise.
#   3. FALSE POSITIVE on ordinary tests. Any non-doctest `add_test` (a script smoke
#      check, a fuzz-corpus runner) was reported as "matches NO test case" and
#      blocked the build. Only entries actually passing `--test-case=` are counted
#      now; everything else is ignored by construction.
#
# The name-set comparison is kept, but as a DIAGNOSTIC printed after a count
# mismatch, not as the test itself.
set -uo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build}"

if [ ! -d "$BUILD_DIR" ]; then
  echo "lint-test-names: no build directory at '$BUILD_DIR' — nothing to check." >&2
  exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Every ctest invocation, verbatim. `-N` lists without running.
if ! ctest --test-dir "$BUILD_DIR" -N -V >"$tmp/ctest-v.txt" 2>/dev/null; then
  echo "ERROR: could not list tests in '$BUILD_DIR'." >&2
  exit 1
fi

# Only the entries that drive a doctest binary. An `add_test` that runs a script is
# not a doctest test case and must not be counted against one (hole 3).
grep 'Test command:' "$tmp/ctest-v.txt" | grep -- '--test-case=' >"$tmp/cases.txt" || true

if [ ! -s "$tmp/cases.txt" ]; then
  # FAIL CLOSED. A repo with doctest tests that registers none is the exact state
  # this script exists to detect; treating it as "nothing to check" is how hole 1
  # let a broken suite through.
  if ctest --test-dir "$BUILD_DIR" -N 2>/dev/null | grep -q 'Total Tests: [1-9]'; then
    echo "ERROR: ctest has tests, but NONE of them invoke a doctest binary with" >&2
    echo "       --test-case=. Either discovery is broken or the binaries changed" >&2
    echo "       shape. Refusing to report a pass on a check that did not run." >&2
    exit 1
  fi
  echo "lint-test-names: no doctest tests registered in '$BUILD_DIR' — nothing to check." >&2
  exit 0
fi

# The binary for each entry is the first field after "Test command: ". Paths are
# printed with backslash-escaped spaces, which is why this unescapes them.
sed -E 's/.*Test command: //; s/ "--test-case=.*//' "$tmp/cases.txt" \
  | sed -E 's/\\ / /g' | sort -u >"$tmp/binaries.txt"

status=0
total=0

while IFS= read -r bin; do
  [ -n "$bin" ] || continue
  if [ ! -x "$bin" ]; then
    echo "ERROR: ctest invokes '$bin', which is not executable." >&2
    status=1
    continue
  fi

  # How many entries ctest will run against this binary.
  registered=$(grep -F -c "Test command: ${bin// /\\ } " "$tmp/cases.txt" || true)

  # How many test cases the binary itself says it has. doctest prints this as a
  # single authoritative integer, so it survives any name containing any separator.
  actual=$("$bin" --list-test-cases 2>/dev/null \
    | sed -n 's/.*unskipped test cases passing the current filters: \([0-9][0-9]*\).*/\1/p' \
    | tail -1)
  if [ -z "$actual" ]; then
    echo "ERROR: '$bin' did not report a test-case count — cannot verify it." >&2
    status=1
    continue
  fi

  total=$((total + actual))

  if [ "$registered" -ne "$actual" ]; then
    echo "ERROR: $bin" >&2
    echo "       contains $actual test case(s) but ctest registered $registered entr(ies)." >&2
    echo "       A registered entry that matches no test case runs ZERO tests and" >&2
    echo "       exits 0 — ctest prints 'Passed' having tested nothing." >&2
    echo "       Most likely cause: a ';' or a newline in a TEST_CASE name, which" >&2
    echo "       CMake and doctestAddTests.cmake both treat as list separators." >&2
    echo "       Names ctest will invoke that the binary does not have:" >&2
    "$bin" --list-test-cases 2>/dev/null \
      | grep -v '^\[doctest\]' | grep -v '^=*$' | grep -v '^[[:space:]]*$' | sort -u >"$tmp/have.txt"
    # ctest -N -V prints the argument shell-escaped, so a name containing a comma
    # comes back as `A\, B`. Unescape before comparing, or the diagnostic buries the
    # one real answer under every comma in the suite.
    grep -F "Test command: ${bin// /\\ } " "$tmp/cases.txt" \
      | sed -E 's/.*--test-case=//; s/"$//' | sed -E 's/\\(.)/\1/g' | sort -u >"$tmp/want.txt"
    comm -13 "$tmp/have.txt" "$tmp/want.txt" | sed 's/^/         /' >&2
    status=1
  fi
done <"$tmp/binaries.txt"

if [ "$status" -eq 0 ]; then
  echo "lint-test-names: $total test case(s) across $(wc -l <"$tmp/binaries.txt" | tr -d ' ') binar(ies), all runnable by ctest."
fi
exit "$status"
