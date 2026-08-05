# Agent Result — automated-suite (UAT Session 3)

**Summary:** Baseline established on a clean scratchpad build (no repo file modified; `git status --porcelain` empty).

GREEN, as claimed: clean Release build in 9s (2s configure + 7s build) with ZERO compiler warnings; `ctest` = **178/178 passed**, exactly the expected count. Suite wall time 6.83–7.30s serial, ~1.5s at `-j12`; no-op rebuild 18ms. Neither build nor suite is remotely slow enough to discourage running them. ASan+UBSan build is completely clean (3722 + 32 assertions, zero diagnostics). Semgrep reports zero findings. Determinism across 14 consecutive full runs, 3 parallel runs and a 5x `--repeat` pass: identical 178-name test list every time, zero failures.

But three of those green signals are weaker than they look, and I could verify exactly how:

1. **8 of the 178 "passing" tests execute nothing, and 4 stage-critical test cases never run at all.** Four doctest cases have a `;` in their name; CMake treats `;` as a list separator, so doctest's discovery splits each into two ctest entries whose `--test-case=` filter matches no test. doctest exits 0 on zero matches, so ctest prints "Passed". `ctest -V` on one of them literally prints `test cases: 0 | 0 passed | 0 failed | 165 skipped` … `Passed`. Real cases: 165+9=174; ctest entries: 178. The four that never execute cover BUG-16 range rejection + last-slide reachability, Esc→privacy blackout→resume, the two-step quit + cancel, and BUG-11/17 resume wording + audience Q&A protection — i.e. the core TM-002/019 defenses. All four pass when invoked correctly, so nothing is broken today, but the gate cannot catch a regression in them. Hits both `.github/workflows/ci.yml:44` and `scripts/run-tests.sh:28`.

2. **The semgrep step is vacuous for this codebase.** `p/owasp-top-ten` ran 5 rules and `p/security-audit` ran 2 rules over the 37 files. Positive control: a C++ file containing a `strcpy` overflow, `sprintf`+`system()` injection, `execl` with user input, a double free and a format-string bug produced **0 findings from both packs**; an inline rule with `languages:[cpp,c]` flagged them instantly. "Semgrep clean" carries no security information here — which matters because the loader parses untrusted .pptx.

3. **One genuinely flaky test on stage-critical code**: `P: a result arriving after cancel is discarded, never delivered late` fails ~3–4% of invocations (20 failures in 600 runs) — about 1 in 25–30 full `ctest` runs would go red. I root-caused it and it is a **test defect, not a product defect**: the product emitted `finished()` exactly once in all 14 instrumented failures, discarded the late result correctly, and never hung (0 hangs in 400 trials); a corrected assertion passed 400/400.

TSan reported 10–12 data races, but I proved them artefacts rather than reporting them as races: a pure-Qt control program containing **zero** product code reproduces the same race class, because QSignalSpy's happens-before edge lives inside uninstrumented QtCore. No product race was found. The flip side is that TSan therefore gives weak assurance across the worker↔GUI boundary — which is also the one part of the product with no test coverage at all (`src/ui/app_shell.cpp`, 301 lines, referenced by no test file).

command_probe: all five commands map correctly and every audience-sentence negative correctly yields no command (including "let's move on to the next slide in our roadmap", "we have about fifteen minutes left", "any questions before we continue", and bare "next"/"pause"/"resume"). 63/65 of my expectations matched; one divergence was my expectation being wrong (interior filler is deliberately never stripped), the other is a real but negligible parser quirk (`go to slide -5` → slide 5).

Artifacts: /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/qa-baseline/ (scripts 01–12, repro23.cpp, repro23b.cpp, tsan_verify.cpp, tsan_control.cpp, all logs).

## Findings (5)

### [SEV-2] 8 of the 178 ctest entries run zero test cases; 4 stage-critical test cases never execute under the gate

**Repro:** # Build (scratchpad build dir used; any Release build works)
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build

# 1. A ctest entry that runs NOTHING and reports Passed:
cd build && ctest -V -R '^A: one past the end is rejected$' 2>&1 | grep -E 'test cases:|Passed'
#   [doctest] test cases: 0 | 0 passed | 0 failed | 165 skipped
#   1/1 Test #102: A: one past the end is rejected ...   Passed    0.02 sec

# 2. The arithmetic: 165 + 9 = 174 real cases, but 178 ctest entries
./tests/pptv_tests --list-test-cases | grep -vE '^\[doctest\]|^=====|^$' | wc -l      # 165
QT_QPA_PLATFORM=offscreen ./tests/ui/pptv_ui_tests --list-test-cases | grep -vE '^\[doctest\]|^=====|^$' | wc -l   # 9
ctest -N | grep -cE '^  Test +#'                                                      # 178

# 3. The 4 real cases that ctest never runs (all contain ';'):
#   A: one past the end is rejected; the last slide is reachable
#   D: Esc goes to the holding screen; a command returns to presenting
#   D: a second Esc asks to quit; cancel returns to holding
#   UAT2 BUG-11/17: 'resume presentation' un-pauses; a bare word does not
# They pass only when invoked directly:
./tests/pptv_tests --test-case='A: one past the end is rejected; the last slide is reachable' | grep 'test cases:'
#   [doctest] test cases: 1 | 1 passed | 0 failed | 164 skipped

# 4. Root cause visible in the generated discovery file:
grep -o 'A: one past the end is rejected\]==\] \[==\[ the last slide is reachable' tests/pptv_tests_tests-*.cmake

**Impact:** Nothing misbehaves on stage today — all four cases pass when actually run, so no live defect is hidden right now. The damage is to the safety net in the final six days: the ctest gate silently does not execute the four test cases that protect the highest-stakes behaviours in the product — BUG-16 range rejection and last-slide reachability (Karl's slide 10 is the end of his talk), Esc→privacy blackout→return-to-presenting (TM-002/019), the deliberate two-step quit and its cancel path (TM-019), and the BUG-11/17 resume wording plus audience-Q&A word protection (TM-002). 21 assertions never run. Any regression touching those paths between now and 2026-08-10 would ship with a fully green 178/178 gate, in both local `scripts/run-tests.sh` and CI (`.github/workflows/ci.yml:44`). Secondary hazard: '178 tests expected green' is not a safe invariant — adding a semicolon to a test name RAISES the count while REMOVING coverage, so the number cannot be used to detect this class of loss.

**Fix:** Remove the semicolons from the four TEST_CASE names — they are purely cosmetic. In tests/test_presentation_controller.cpp: line 58 'A: one past the end is rejected; the last slide is reachable', line 199 'D: Esc goes to the holding screen; a command returns to presenting', line 210 'D: a second Esc asks to quit; cancel returns to holding'; and tests/test_recognizer_controller.cpp:170 'UAT2 BUG-11/17: 'resume presentation' un-pauses; a bare word does not'. Replace ';' with ' and ' or an em dash (commas are already handled correctly by doctest.cmake's escaping — only ';' breaks). Then assert the invariant instead of the count: verify that the number of ctest entries equals the number of real doctest cases, e.g. add a CI step comparing `ctest -N | grep -cE '^  Test +#'` against the summed `--list-test-cases` counts of both binaries, and fail the build on mismatch. That check would also catch any future name containing a semicolon.

### [SEV-3] The p/owasp-top-ten and p/security-audit semgrep packs contain almost no C/C++ rules — the security gate is vacuous for this codebase

**Repro:** # Both packs over src/ report 0 findings, but note the rule counts:
semgrep --config p/owasp-top-ten  --metrics=off --no-git-ignore src/ 2>&1 | grep -E 'Rules run:|Targets scanned:|Findings:'
#   Findings: 0 (0 blocking) / Rules run: 5 / Targets scanned: 37
semgrep --config p/security-audit --metrics=off --no-git-ignore src/ 2>&1 | grep -E 'Rules run:|Targets scanned:|Findings:'
#   Findings: 0 (0 blocking) / Rules run: 2 / Targets scanned: 37

# POSITIVE CONTROL — a C++ file stuffed with textbook vulnerabilities:
mkdir -p /tmp/sgctl/src && cat > /tmp/sgctl/src/bad.cpp <<'EOF'
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
void f(const char* userInput) {
    char buf[16];
    strcpy(buf, userInput);            // CWE-120 overflow
    char cmd[256];
    sprintf(cmd, "ls %s", userInput);  // CWE-78 injection
    system(cmd);
    execl("/bin/sh", "sh", "-c", userInput, NULL);
    char* p = (char*)malloc(10); free(p); free(p);   // double free
    printf(userInput);                 // format string
}
EOF
semgrep --config p/owasp-top-ten  --metrics=off --no-git-ignore /tmp/sgctl/src   # 0 findings
semgrep --config p/security-audit --metrics=off --no-git-ignore /tmp/sgctl/src   # 0 findings

# ...yet semgrep's C++ engine works fine when given C++ rules:
cat > /tmp/sgctl/inline.yml <<'EOF'
rules:
  - id: test-strcpy
    languages: [cpp, c]
    severity: ERROR
    message: strcpy used
    pattern: strcpy(...)
  - id: test-system
    languages: [cpp, c]
    severity: ERROR
    message: system() used
    pattern: system(...)
EOF
semgrep --config /tmp/sgctl/inline.yml --metrics=off --no-git-ignore /tmp/sgctl/src   # 2 Code Findings

**Impact:** No direct stage impact, but it means one of the project's standing security gates has been returning a false all-clear. Those two registry packs are overwhelmingly web-language rules; only 5 and 2 language-agnostic rules respectively even applied to the 37 C++ files. The control proves they would not flag a stack overflow, command injection, double free or format-string bug if one existed. This matters specifically because src/loader/deck_loader.cpp parses an UNTRUSTED .pptx (zip + XML, with hostile bad_*.pptx fixtures in-tree) — precisely the attack surface these packs are being trusted to cover, and precisely the code they cannot see. Any future reviewer reading 'semgrep p/owasp-top-ten + p/security-audit: clean' would reasonably but wrongly conclude the parsing path had been statically checked.

**Fix:** Stop treating these two packs as the C++ security gate — either drop them or record honestly that they scan 5 and 2 applicable rules. Replace with tooling that actually covers C++: `clang-tidy` with the `clang-analyzer-*`, `bugprone-*`, `cert-*` and `cppcoreguidelines-*` checks (the tree already sets CMAKE_EXPORT_COMPILE_COMMANDS ON for exactly this), and/or `cppcheck --enable=warning,portability`. Given the untrusted-input surface, the highest-value addition is a libFuzzer or AFL++ harness over DeckLoader::load() seeded with tests/fixtures/bad_*.pptx and good_*.pptx, run under ASan+UBSan — that would exercise the zip/XML path far more thoroughly than any static pack. Keep the existing local .semgrep/ rules, which are project-authored and do apply.

### [SEV-3] Flaky test: 'P: a result arriving after cancel is discarded, never delivered late' fails ~3-4% of runs due to a racy QSignalSpy assertion (product code is correct)

**Repro:** # As ctest invokes it — 19 failures in 450 runs (4.22%); 20/600 combined:
cd build
f=0; for i in $(seq 1 450); do \
  ./tests/pptv_tests --test-case='P: a result arriving after cancel is discarded\, never delivered late' >/dev/null 2>&1 || f=$((f+1)); \
done; echo "$f failures / 450"

# Or surface it through ctest directly (failed on iteration 3 of 40 for me):
ctest --repeat until-fail:40 -R '^(O|P):'

# The verbatim failure:
#   tests/test_deck_load_worker.cpp:118: FATAL ERROR: REQUIRE( finishedSpy.wait(5000) ) is NOT correct!
#     values: REQUIRE( false )
#   test time = 4.83 sec   (the full 5s timeout)

# PROOF it is the assertion, not the product — instrumented repro:
#   /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/
#     05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/qa-baseline/repro23b.cpp
# mode 0 = the assertion as written; mode 1 = an assertion that tolerates an
# already-delivered signal. Same product code in both.
#   mode 0: 14 FAIL / 400 runs. Every failure printed:
#           finishedHits=1  loadedHits=0  loadFnEntered=1  (and 0 thread hangs)
#   mode 1: 0 FAIL / 400 runs, max wait 15ms.

**Impact:** The product is correct here and I verified it rather than assuming: DeckLoadWorker::start() emits finished() on all three exit paths, and in all 14 instrumented failures it had emitted finished() exactly once, had correctly emitted loaded() zero times (the late result WAS discarded, which is the behaviour the test is named for), and the worker thread always exited cleanly — 0 hangs in 400 trials. So there is no on-stage hang or late-slide-flash risk from this. The impact is entirely on the pre-flight gate in the final six days: roughly 1 in every 25-30 full `ctest` runs goes red spuriously, and each failure costs a 5-second stall. On a slower, noisier CI runner the rate will be higher. The real danger is behavioural — a gate that cries wolf trains the team to re-run it and shrug, which is exactly how a genuine regression in the deck-load cancel path gets waved through on the morning of the talk.

**Fix:** The assertion is racy by construction: QSignalSpy::wait() snapshots the count on entry and returns true only if a NEW emission arrives afterwards, but here the worker is expected to emit finished() the instant gate.release() returns — concurrently with the main thread reaching wait(). When the worker wins the race, wait() blocks the full 5s and returns false even though the signal arrived. Fix in tests/test_deck_load_worker.cpp:118 by tolerating an already-delivered signal, e.g. replace `REQUIRE(finishedSpy.wait(5000));` with a deadline poll: `QDeadlineTimer dl(5000); while (finishedSpy.isEmpty() && !dl.hasExpired()) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); } REQUIRE(finishedSpy.count() >= 1);` (Qt's QTRY_VERIFY macro does exactly this if QtTest's macros are available). Note the same latent pattern exists wherever a QSignalSpy::wait() is called after an action that can complete synchronously — worth a sweep of the other spy.wait() sites in tests/test_deck_load_worker.cpp and tests/test_pre_render_worker.cpp. Also consider replacing the `QThread::msleep(50)` on line 115 with a semaphore the load fn signals on entry, which removes the timing assumption entirely.

### [SEV-3] src/ui/app_shell.cpp (301 lines wiring both workers to the GUI thread) has zero test coverage, and TSan cannot certify that boundary either

**Repro:** # No test file references AppShell or PresentationWindow construction:
grep -rln 'app_shell\|AppShell' tests/            # no output
grep -rln 'PresentationWindow' tests/             # only tests/ui/test_widgets.cpp
wc -l src/ui/app_shell.cpp                        # 301

# The 9 widget tests cover slide_surface / notice_strip / key routing only:
grep -n 'TEST_CASE' tests/ui/test_widgets.cpp

# And TSan's assurance across the Qt signal boundary is weak — a pure-Qt control
# with ZERO product code reproduces the same 'race' the product build reports:
#   /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/
#     05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/qa-baseline/tsan_control.cpp
# control: 2 racing runs / 60, SUMMARY: data race qarraydatapointer.h:36
# product: 2 racing runs / 12, SUMMARY: data race qvariant.h:968 in
#          qvariant_cast<pptv::DeckLoadOutcome>  (all from test_deck_load_worker.cpp:143)
# Same product path with a TSan-VISIBLE sync edge (std::mutex + join): 0 warnings
#   see tsan_verify.cpp in the same directory — 'verified 8/8 error kinds'

**Impact:** The brief names the two workers plus the GUI thread as the highest-risk interaction in the product, and that interaction is assembled in app_shell.cpp — which no test ever constructs or executes. So the DeckLoadWorker and PreRenderWorker are each well tested in isolation (groups P and O, 19 cases), but the code that actually connects them to the window, marshals QImage onto the GUI thread and turns it into QPixmap, and enforces 'QPixmap must never be touched off the GUI thread', is exercised only by launching the real app by hand. Compounding it, ThreadSanitizer cannot close the gap by inspection: Qt is not TSan-instrumented, so QMutex fast paths and Qt's atomic refcounts are invisible, which produces false positives (proven above) and, more importantly, means TSan's silence across that boundary is not evidence of safety. A TM-018 violation or a cross-thread QPixmap touch introduced in app_shell.cpp between now and the talk would be caught by neither the 178-test gate nor the sanitizer builds — only by someone noticing a frozen or black projector during a rehearsal.

**Fix:** Add a small headless integration test that constructs AppShell under QApplication with QT_QPA_PLATFORM=offscreen, feeds it tests/fixtures/good_text.pptx, and asserts the observable contract: that loaded/rendered signals arrive on the GUI thread (QThread::currentThread() == qApp->thread() inside the slot), that a slide raster reaches the surface, that over-cap slides become placeholders without entering the renderer, and that cancelling a load mid-flight leaves the shell in a sane state. Even three or four such cases would cover the wiring that 301 untested lines currently carry. Separately, do not add TSan suppressions for the 10-12 qvariant_cast warnings and do not chase them as bugs — they are QSignalSpy artefacts; instead record in the QA notes that TSan gives only weak assurance through Qt, so the worker-to-GUI contract must be asserted by explicit thread-affinity checks in tests rather than inferred from a clean TSan run.

### [SEV-4] 'go to slide -5' jumps to slide 5 — a leading minus sign is silently consumed as a token separator

**Repro:** ./build/command_probe 'go to slide -5'
#   "go to slide -5"  ->  GoToSlide(5)

# Cause: src/command/number_parser.cpp:parseSlideNumber splits on [\s\-]+ so that
# hyphenated number words ("twenty-five") work; a leading '-' therefore becomes a
# separator rather than a sign, and "-5" tokenises to ["5"].

# Neighbouring inputs are all handled correctly:
./build/command_probe 'go to slide 2147483648' 'go to slide 999999999999999999999' 'go to slide banana' 'go to slide'
#   all -> (no command)

**Impact:** Negligible for the live talk, and I want to be clear about that rather than inflate it. The voice path cannot produce this string — a speech recognizer emits number words, and the F4 parser already rejects overflow, token floods and malformed sequences correctly. The keyboard path does not route through this parser at all (key_translator handles typed digits separately, and its digit buffer is capped). So reaching it requires typing a literal minus into command_probe, which is a dev tool. It is reported only because it is a real divergence between what the grammar accepts and what it appears to accept: a negative slide number is not rejected, it is silently reinterpreted as its absolute value, and the presentation_controller then range-checks 5 as perfectly valid in a 10-slide deck. If a future input source ever forwards raw text containing a sign, this would become a silent wrong jump rather than the loud rejection the design intends everywhere else.

**Fix:** In src/command/number_parser.cpp:parseSlideNumber, reject a sign character rather than swallowing it: before the split, return std::nullopt if the trimmed input begins with '-' or '+', or narrow the split regex so that '-' is only a separator when it sits between two word characters (e.g. split on \s+ and separately on (?<=\w)-(?=\w)). Guard it with a test alongside the existing 'audit F1: overflow / token flood rejects, never negative' case, which currently covers overflow but not an explicit sign. Very low priority — do not let this compete with the gate findings above for the remaining six days.

## Could not break

- FULL SUITE PASS COUNT — exactly as claimed. `ctest` reports '100% tests passed, 178 tests' every single time. Clean Release build produced ZERO compiler warnings and zero errors (build.log grepped for 'warning:|error:' — no hits across 55 targets).
- DETERMINISM ACROSS NORMAL RUNS — 14 consecutive full ctest runs, all green (4 requested + 10 more). The test-name list is byte-identical across runs (diff'd run 1 vs runs 2/3/4 — 'identical'). 3 runs at -j12: green. `ctest --repeat until-fail:5` over the whole suite: green. The only non-determinism found is the single flaky test reported as a finding, which needed `--repeat until-fail:40` on the worker subset or ~450 direct invocations to surface.
- ASan + UBSan — COMPLETELY CLEAN. Built with -fsanitize=address,undefined -fno-sanitize-recover=undefined. pptv_tests: 165 cases / 3722 assertions, all passed. pptv_ui_tests: 9 cases / 32 assertions, all passed. Zero AddressSanitizer errors, zero 'runtime error:' UBSan diagnostics, zero SUMMARY lines. CAVEAT worth recording: LeakSanitizer is unsupported on this platform ('AddressSanitizer: detect_leaks is not supported on this platform'), so memory LEAKS were not checked at all — relevant given BUG-22's unbounded raster cache is a known deferred item.
- THREADSANITIZER — no product race found, and I verified rather than assumed. The 10-12 intermittent warnings (2 of 12 runs) all share one signature: qvariant_cast<pptv::DeckLoadOutcome> read on the main thread vs QVariant construction on the worker thread, all originating at tests/test_deck_load_worker.cpp:143. Two controls prove they are artefacts: (a) tsan_verify.cpp exercises the identical product path (DeckLoadWorker::start on a real QThread, all 8 error kinds crossing the boundary) with a TSan-visible std::mutex + join edge — ZERO warnings, 'verified 8/8 error kinds'; (b) tsan_control.cpp contains NO powerpoint-voice code whatsoever — a plain QObject emitting a custom-metatype struct read back through QSignalSpy exactly as the test does — and reproduces the same race class (2 of 60 runs, qarraydatapointer.h:36). Root cause: QSignalSpy's happens-before edge is QMutex plus Qt's atomic refcounts, both inside an uninstrumented QtCore, so TSan cannot see the ordering. pptv_ui_tests under TSan: 0 warnings.
- DECK-LOAD CANCEL SEMANTICS — genuinely correct under adversarial timing. Across 400 instrumented trials of the cancel-mid-parse scenario: finished() emitted exactly once on every run, loaded() emitted zero times every run (the late result really is discarded, never delivered late over what the presenter moved on to), and the worker thread exited cleanly every time — 0 hangs. All three exit paths in DeckLoadWorker::start() emit finished(). I tried to make it hang and could not.
- VOICE COMMAND GRAMMAR — held up against every audience-sentence negative I threw at it. All five commands map correctly including natural phrasings ('okay next slide', 'next slide please', 'so next slide everyone', 'pause the presentation', 'resume the presentation'), case/whitespace/punctuation variants ('Next slide.', '  NEXT   SLIDE  ', 'GO TO SLIDE THREE'), and number forms ('go to slide ten', 'go to slide one five' -> 15, 'go to slide forty two' -> 42, 'go to slide one hundred'). Every audience/negative case correctly yielded no command: 'let's move on to the next slide in our roadmap', 'move to the next slide', 'I want to talk about the next slide', 'the next slide shows our revenue', 'on the previous slide we saw a decline', 'can you go back to the previous slide', 'we should pause and think about this', 'let's pause for questions', 'any questions before we continue', 'we have about fifteen minutes left', 'slide fifteen was the important one', 'that was slide ten', 'this presentation is about our next quarter', bare 'next'/'previous'/'pause'/'continue'/'resume'/'slide', 'go to slide' with no number, 'go to slide banana', empty/whitespace/pure-punctuation input, and both integer-overflow attempts ('go to slide 2147483648', 'go to slide 999999999999999999999'). The BUG-17 single-word Q&A protection holds.
- BUILD AND SUITE SPEED — no deterrent whatsoever, measured twice. Clean from-scratch build: 9 seconds (2s configure including the doctest FetchContent, 7s compile of all 55 targets on 12 cores). No-op rebuild: 18 milliseconds. Full suite: 6.83-7.30s serial, ~1.5s at -j12. Sanitizer builds are equally cheap (ASan 11s, TSan 10s). There is no speed-based excuse for skipping either gate before the talk.
- SEMGREP RAN WITHOUT ERRORS — 0 findings, 0 parse errors, 37 files scanned, on both p/owasp-top-ten and p/security-audit. I could not get either pack to flag anything in src/ — but as reported above, I also could not get them to flag a deliberately vulnerable C++ file, so this clean result is vacuous rather than reassuring and should not be quoted as a security signal.
- REPOSITORY UNTOUCHED — all builds and scratch artifacts went to /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/qa-baseline/. `git status --porcelain` in the repo returns empty; 0 modified tracked files. The Confidential real deck was never located, opened, read, copied or rendered — only tests/fixtures/*.pptx were used.
