# Agent Result — cross-platform (UAT Session 3)

**Summary:** CI on main is GREEN for the real gate and permanently RED for a stub. Latest main run (d0c433a, "Merge PR #16 / F7b"): CI run 30959310972 — job `test` SUCCESS 1m27s (https://github.com/kraulerson/powerpoint-voice/actions/runs/30959310972/job/92159466139), job `sast` SUCCESS 26s, semgrep 1.171.0, 0 findings (.../job/92159466101). Alongside it, run 30959310452 (.github/workflows/release.yml) FAILED in 0s — and has failed on all 8 recent pushes, on every branch. Note local HEAD is walk/uat-3 (a1e7069, one unpushed docs commit ahead of origin/main); no CI has run on it.

Q2 (the new pptv_ui_tests binary on Ubuntu headless) — IT WORKS, verified empirically, not by inference: the CI log shows tests 170-178 (the nine "U:" widget tests) Passed on ubuntu-24.04 with apt Qt 6.4.2, 178/178, 3.77s. QT_QPA_PLATFORM is NOT set anywhere in ci.yml. It works because tests/ui/ui_test_main.cpp:9 and tests/test_main.cpp:9 both call qputenv("QT_QPA_PLATFORM","offscreen") before constructing QApplication/QGuiApplication. The offscreen plugin does arrive from qt6-base-dev — libqt6gui6t64 and qt6-qpa-plugins are hard Depends (they installed under --no-install-recommends; visible in the run log). No YAML fix is REQUIRED. A one-line defensive fix is recommended (finding 2) because the Build step, not just Test, executes those binaries.

Q3 (clang-tidy) — ran the focused set (bugprone-*, cert-*, clang-analyzer-*, performance-*) over src/present/ and src/ui/. Honest answer: a real lint gate would find NOTHING new there. Zero bugprone, zero cert, zero clang-analyzer, zero concurrency findings. The only substantive hit is the already-known BUG-23 const-std::move at src/present/deck_load_worker.cpp:83; the other three are micro-perf suggestions in app_shell.cpp, one of which (QImage by value) is effectively a false positive because QImage is copy-on-write. What a naive gate WOULD do is break CI — 837 diagnostics under CI's --warnings-as-errors='*' (finding 1).

Q4 (clean build) — fresh clone of main into scratch, only the documented env: configure + build exit 0; ctest 178/178; scripts/run-tests.sh exit 0 (it is the commit gate and it still passes); the app runs (--version prints "powerpoint-voice 0.1.0", opens tests/fixtures/good_text.pptx headless). No deck path or deck text reaches stdout/stderr — checked with a deliberately named /tmp/CONFIDENTIAL-Q3-BOARD-DECK.pptx, never echoed.

Q5 (git-LFS) — no risk. Nothing in CMakeLists.txt or src/ references vosk. A clone with the LFS objects absent is 10 MB of pointer text and builds + tests 100% green. CI checks out with `lfs: false` (confirmed in the runner log), so it never downloads the 76 MB. CI does not need LFS and will not need it until Vosk is actually linked.

## Findings (7)

### [SEV-3] CI's clang-tidy gate is dead (OBS-016 confirmed), and enabling it as ci.yml is written today would immediately turn CI red

**Repro:** cd <repo>; ls .clang-tidy  -> No such file or directory
gh run view --job=92159466139 --log | awk -F'\t' '{print $2}' | uniq   # 19 steps; "Lint (clang-tidy)" is NOT among them — the if: hashFiles('.clang-tidy') != '' guard skips it silently

Simulating what happens when it IS enabled (done in a scratch clone, repo untouched):
  printf 'Checks: >\n  -*,\n  bugprone-*,\n  cert-*,\n  clang-analyzer-*,\n  performance-*\nHeaderFilterRegex: %s\n' "'src/.*'" > .clang-tidy
  git ls-files '*.cpp' '*.cc' '*.cxx' ':(exclude)third_party/**' | xargs -r -n 8 -P 2 clang-tidy -p build --warnings-as-errors='*'
  -> exit 1, 837 diagnostics.
Breakdown: 623 cert-err33-c (ALL in tests/ — 121 test_deck_loader, 118 test_presentation_controller, ...), 67 performance-enum-size, 39 bugprone-macro-parentheses (ALL from build/_deps/doctest-src/doctest/doctest.h), 18 bugprone-unchecked-optional-access (ALL in tests/), 14 bugprone-easily-swappable-parameters, 10 bugprone-exception-escape (7 doctest.h + the three main()s).
Gotcha worth knowing: HeaderFilterRegex 'src/.*' is unanchored and therefore also matches build/_deps/doctest-**src**/doctest/doctest.h — that one mistake alone contributes ~50 of the 837.

What the gate actually finds in the NEW code (src/present + src/ui), with a working compile db:
  clang-tidy -p build --extra-arg="-isysroot$(xcrun --show-sdk-path)" --checks='-*,bugprone-*,cert-*,clang-analyzer-*,performance-*' --header-filter='src/(present|ui)/.*' src/present/*.cpp src/ui/*.cpp
  -> 12 warnings, 0 errors:
   * src/present/deck_load_worker.cpp:83:69 performance-move-const-arg — std::move on a const LoadResult; this IS the known BUG-23 item, so the whole deck is deep-copied instead of moved
   * src/ui/app_shell.cpp:71:28 performance-for-range-copy (QPointer copy in a 2-element loop — noise)
   * src/ui/app_shell.cpp:107:45 performance-unnecessary-value-param (DeckLoadOutcome by value — holds a shared_ptr, one refcount bump per load)
   * src/ui/app_shell.cpp:215:53 performance-unnecessary-value-param (QImage by value; QImage is copy-on-write so this is a refcount bump, NOT a raster copy — effectively a false positive)
   * 8 x performance-enum-size (pure style)
Also ran -*,concurrency-*,misc-*,bugprone-*,cert-* over the same files: zero concurrency findings, zero bugprone, zero cert.

**Impact:** No lint gate is running before the talk, so the green check on main overstates what CI verified. The stage-day risk is the FIX, not the gap: switching the gate on naively in the last 6 days turns main red with 837 diagnostics, ~99% of them noise from test files and from doctest.h, and someone under time pressure then either disables the gate again or starts editing shipping code to silence style warnings. The gate itself would have caught nothing in the presenter code that is not already logged as BUG-23.

**Fix:** Land .clang-tidy scoped to product code, and narrow the ci.yml lint step to src/ so tests and doctest.h cannot fail the build. Exact .clang-tidy:

  Checks: >
    -*,
    bugprone-*,
    cert-*,
    clang-analyzer-*,
    performance-*,
    -bugprone-easily-swappable-parameters,
    -bugprone-exception-escape,
    -performance-enum-size
  HeaderFilterRegex: '(^|/)src/(core|loader|render|command|model|present|ui)/'
  WarningsAsErrors: ''

(Note the anchored HeaderFilterRegex — it is what keeps doctest-src out.) Exact ci.yml replacement for the Lint step:

      - name: Lint (clang-tidy)
        if: ${{ hashFiles('.clang-tidy') != '' && hashFiles('CMakeLists.txt') != '' }}
        run: |
          git ls-files 'src/*.cpp' \
            | xargs -r -n 8 -P 2 clang-tidy -p build --warnings-as-errors='*'

With that scoping the only remaining hit is the BUG-23 const std::move, so fix that (drop the `const` on `LoadResult r` in deck_load_worker.cpp:77) and the gate goes green. Given the date, landing this AFTER 2026-08-10 is the defensible call — but do not leave the step silently skipped without a note.

### [SEV-3] Ubuntu CI headlessness rests on one hardcoded qputenv line in test code; the Build step (not just Test) executes the widget binary, and the hardcode overrides any CI-set platform

**Repro:** 1) ci.yml has no QT_QPA_PLATFORM anywhere:
   grep -n QT_QPA_PLATFORM .github/workflows/ci.yml   -> no match
2) The only thing making it work is tests/ui/ui_test_main.cpp:9 and tests/test_main.cpp:9:
   qputenv("QT_QPA_PLATFORM", "offscreen");   // before QApplication/QGuiApplication
3) The BUILD depends on it too — doctest_discover_tests registers a POST_BUILD command that RUNS the binary:
   grep -n 'POST_BUILD' build/_deps/doctest-src/scripts/cmake/doctest.cmake   -> line 136: TARGET ${TARGET} POST_BUILD
   ui_test_main.cpp constructs QApplication before ctx.run(), so --list-test-cases at build time constructs a QApplication.
4) Proof the hardcode beats the environment (so CI cannot select any other platform):
   QT_QPA_PLATFORM=xcb  ./build/tests/ui/pptv_ui_tests --list-test-cases   -> runs fine, lists all 9 U: tests
   QT_QPA_PLATFORM=totally_bogus ./build/tests/pptv_tests --list-test-cases -> runs fine
   Both should abort if the env var were honoured.

**Impact:** Today: nothing — main is green, 178/178, the nine U: tests included. Tomorrow: whoever tidies up 'why is a test hardcoding an environment variable' and deletes that line breaks `cmake --build build` on Ubuntu CI, not just ctest, and the failure message ('Could not load the Qt platform plugin "xcb"') looks nothing like a test failure — a confusing red 6 days before the talk. Second-order: because qputenv wins, CI can never run the widget tests under xvfb/xcb, so the offscreen QPA is the only surface these tests ever see. That matters for what the U: tests can prove — offscreen has no window manager (the app itself logs 'This plugin does not support raise()' and 'does not support propagateSizeHints()'), so AppShell's showFullScreen / setScreen / raise / activateWindow path (src/ui/app_shell.cpp:194-208 and moveWindowToNextScreen at :251) — the Ctrl+Shift+D projector escape hatch — is covered by zero automated tests on any platform.

**Fix:** Belt-and-braces, costs nothing, put the env on BOTH steps (Build runs the binaries too):

      - name: Build
        if: ${{ hashFiles('CMakeLists.txt') != '' }}
        env:
          QT_QPA_PLATFORM: offscreen
        run: cmake --build build

      - name: Test
        if: ${{ hashFiles('CMakeLists.txt') != '' }}
        env:
          QT_QPA_PLATFORM: offscreen
        run: ctest --test-dir build --output-on-failure

Separately, in tests/ui/ui_test_main.cpp prefer qputenv only when unset, so CI can override:
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
Do NOT do the second half before 2026-08-10 — it changes the one line CI currently depends on. The env: addition alone is safe today and makes the removal survivable later. The fullscreen/multi-screen path stays a hand-UAT item: no headless CI can cover it.

### [SEV-3] .github/workflows/release.yml is an invalid workflow file — every push to every branch logs a permanent red run

**Repro:** gh run list --workflow='.github/workflows/release.yml' --limit 5
  -> 5/5 completed failure, 0s each, on main AND on feature branches
gh run view 30959310452
  -> 'X This run likely failed because of a workflow file issue.'
Root cause is in the file itself, .github/workflows/release.yml:
      - uses: # TODO: Add setup action for your language
        with:
          version: 'latest'
  A `uses:` key with an empty value is a schema error, so GitHub cannot parse the workflow and records a 0s failure against the push. `on: push: tags: ['v*']` does NOT prevent this — invalid workflows fail on the triggering push regardless of the trigger filter.
Every push in the last 12 hours produced this pair: 30959310452, 30958468723, 30954183772, 30953991667, 30945541603, 30937326630, 30916592644.

**Impact:** github.com/kraulerson/powerpoint-voice always shows a failing check on main. Six days out, that is the classic way a real failure gets ignored — the CI job that matters (run 30959310972, green) sits next to a red X that everyone has learned to scroll past. It also means no signed/checksummed build can ever be produced; if anything goes wrong on 2026-08-10 there is no released artefact to fall back to, only Karl's build tree.

**Fix:** Either park the pipeline or complete it. Parking is the 6-days-out answer — one edit, makes the noise stop:

  on:
    workflow_dispatch:      # tags: ['v*'] re-enabled when the TODOs below are filled in

and delete the invalid step body (the `- uses: # TODO ...` block and the empty `run: # TODO ...` steps) or replace them with `run: echo 'TODO'`. The SBOM step at the bottom already `exit 1`s deliberately, so workflow_dispatch alone still cannot produce a bad release. Do NOT try to actually wire up code signing before the talk.

### [SEV-4] Two CI governance steps call scripts that do not exist and swallow the failure with `|| true`

**Repro:** ls scripts/check-changelog.sh scripts/check-session-state.sh
  -> ls: scripts/check-changelog.sh: No such file or directory
  -> ls: scripts/check-session-state.sh: No such file or directory
find . -path ./build -prune -o -name 'check-changelog*' -print -o -name 'check-session*' -print   -> nothing
.github/workflows/ci.yml:
      - name: Governance - Changelog check
        run: bash scripts/check-changelog.sh 2>/dev/null || true
      - name: Governance - Session state check
        run: bash scripts/check-session-state.sh 2>/dev/null || true
Both steps DO appear in the run's step list (steps 16 and 17 of 19 on job 92159466139) and both are green with zero output — 2>/dev/null eats 'No such file or directory' and || true eats the exit code.

**Impact:** Two named, green checkmarks in the CI UI verify nothing at all. This is the exact anti-pattern the same file's Phase-gate comment explicitly forbids ('do not soften it with || echo / || true: that discards the verdict'). No stage impact — but it is the same species as the dead clang-tidy step, and it means the green tick on main covers less than it appears to. Worth knowing before anyone treats CI-green as sufficient sign-off for the talk.

**Fix:** Either delete the two steps, or make them fail loudly like the Phase-gate step already does:

      - name: Governance - Changelog check
        run: |
          if [ ! -f scripts/check-changelog.sh ]; then
            echo "::error::scripts/check-changelog.sh missing — a check that cannot run must not pass"; exit 1
          fi
          bash scripts/check-changelog.sh

(same shape for check-session-state.sh). Deleting them is the honest 6-days-out choice; loud-failing them is the right end state.

### [SEV-4] macOS: clang-tidy cannot parse the project's own compile database, and silently reports a DIFFERENT finding set from the broken AST

**Repro:** export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
clang-tidy -p build --checks='-*,bugprone-*,cert-*,clang-analyzer-*,performance-*' --header-filter='src/(present|ui)/.*' src/present/*.cpp src/ui/*.cpp
  -> 11/11 files 'Error while processing', e.g.
     src/present/deck_load_worker.hpp:3:10: error: 'atomic' file not found
     /opt/homebrew/opt/qt/lib/QtCore.framework/Headers/qsystemdetection.h:55:12: error: 'TargetConditionals.h' file not found
Cause: CMake does not emit -isysroot for AppleClang, so the entry for app_shell.cpp is just
  /usr/bin/c++ ... -O3 -DNDEBUG -std=c++20 -arch arm64 -c .../app_shell.cpp
and Homebrew's clang-tidy has no SDK. AppleClang finds it implicitly; clang-tidy does not.
The dangerous part is that clang-tidy does not stop — it reports from the broken AST, and the report differs:
  clang-tidy -p build --checks='-*,bugprone-easily-swappable-parameters' src/present/pre_render_worker.cpp
    -> src/present/pre_render_worker.cpp:30:30: warning: 2 adjacent parameters of 'renderOrder' ... easily swapped
  clang-tidy -p build --extra-arg="-isysroot$(xcrun --show-sdk-path)" --checks='-*,bugprone-easily-swappable-parameters' src/present/pre_render_worker.cpp
    -> (no output)
Same flags, same file: one finding vs none, purely from whether the sysroot resolves. The app_shell QImage diagnostic also moves from 211:47 to 215:53 between the two.

**Impact:** Only bites if/when finding 1's .clang-tidy lands, but then it bites hard on Karl's machine specifically: the local run reports compiler errors on every file while CI (Ubuntu, gcc-generated compile db, libstdc++ on the default include path) runs clean, so macOS and CI disagree about the same commit. Worse, the broken run still prints plausible-looking findings, so someone can spend the last few days 'fixing' a warning that does not exist. It also explains why any macOS-side lint attempt would look catastrophically broken and get abandoned.

**Fix:** Two options, both one line. Either make the compile db self-describing at configure time:
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)"
(the sysroot then appears in compile_commands.json and clang-tidy needs no extra flags on either platform), or pass it at invocation in whatever local lint wrapper gets added:
  clang-tidy -p build --extra-arg="-isysroot$(xcrun --show-sdk-path)" ...
Prefer the CMAKE_OSX_SYSROOT form — it fixes every consumer of the database (editors, clangd) at once. Note scripts/run-tests.sh currently runs clang-format only, never clang-tidy, so nothing is broken today.

### [SEV-4] Launching without a deck argument — and every failed load — dead-ends on a window with no controls

**Repro:** # built from a clean clone, documented recipe
QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice
  -> process stays up; StartView shown
QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice tests/fixtures/bad_notzip.pptx
  -> warning dialog, then back to StartView; process stays up
src/ui/start_view.cpp is the whole view: setWindowTitle + two QLabels ('powerpoint-voice', 'No deck loaded'). No QPushButton, no QFileDialog, no setAcceptDrops.
src/main.cpp:26-31 — a deck is opened ONLY from argv; with no positional argument it calls shell.showStart() and stops.
src/ui/app_shell.cpp:107-118 — on load failure it shows a fixed-string QMessageBox then start_->show(), i.e. returns to the same dead end.
Side checks that came out clean: '-psn_0_123456' (legacy Finder launch argument) does NOT trip QCommandLineParser — Qt strips it, app starts normally; an unknown option exits rc=1 with 'powerpoint-voice: Unknown option ...'; and no deck path or deck text ever reaches stdout/stderr (verified with a path deliberately named /tmp/CONFIDENTIAL-Q3-BOARD-DECK.pptx — never echoed).

**Impact:** If Karl double-clicks the .app in Finder on stage day, or drags the .pptx onto its icon (macOS sends a QFileOpenEvent, which nothing in the app handles), he gets a dark 720x480 window saying 'No deck loaded' with nothing to click. Same if the deck fails to load once — dismiss the dialog and he is back at the dead end with no retry. Recovery is: quit, switch to a terminal, retype the full path. That is a bad 30 seconds in front of an audience. Note the documented recipe does pass the deck on the command line, so this only fires if he deviates from it or a load fails.

**Fix:** Cheapest safe change for this week is a single button on StartView that opens QFileDialog::getOpenFileName(this, tr("Open deck"), QString(), tr("PowerPoint (*.pptx)")) and calls AppShell::openDeck on the result — src/ui/start_view.cpp already has the QVBoxLayout to drop it into, and AppShell::openDeck (app_shell.cpp:190) is already re-entrant because it calls teardownWorkers() first. Second-cheapest, and zero code: make the operational answer a shell alias / .command launcher that hardcodes the deck path, and rehearse launching only that way. If neither lands before 2026-08-10, put 'always launch from the terminal with the deck path' in the stage runbook explicitly.

### [SEV-4] The macOS .app bundle is non-relocatable and ad-hoc signed — it only runs on the machine that built it

**Repro:** otool -L build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice
  /opt/homebrew/opt/libzip/lib/libzip.5.dylib
  /opt/homebrew/opt/pugixml/lib/libpugixml.1.dylib
  /opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets
  /opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui
  ... (absolute Homebrew paths, no @rpath, no bundled frameworks)
find build/powerpoint_voice.app -type f   -> exactly two files: Contents/Info.plist and Contents/MacOS/powerpoint_voice
codesign -dv build/powerpoint_voice.app   -> Signature=adhoc, TeamIdentifier=not set, Sealed Resources=none
spctl -a -t exec -vv build/powerpoint_voice.app -> 'code has no resources but signature indicates they must be present'
Info.plist CFBundleIdentifier is still the template value: com.yourcompany.4748f7c7.powerpoint_voice

**Impact:** Zero impact if Karl presents from the build tree on his own Mac, which is what the recipe says and what I verified end to end. It becomes a talk-ender only in the scenario where the laptop dies and someone copies the .app to a colleague's machine — it will not launch there (missing Homebrew Qt), and Gatekeeper would block it anyway. Worth knowing so nobody plans that as the contingency.

**Fix:** No code change needed before the talk — instead make the fallback plan explicit and correct: the backup is 'clone the repo and rebuild with the documented recipe on a Mac with `brew install qt libzip pugixml`', NOT 'copy the .app'. A fresh clone + build takes about 90 seconds (measured). If a genuinely portable bundle is ever wanted, that is `macdeployqt build/powerpoint_voice.app` plus a real CFBundleIdentifier and a Developer ID signature — a post-talk task, and it belongs in release.yml (finding 3) rather than in a rush this week.

## Could not break

- Ubuntu CI headless widget tests. This was the main thing I was sent to break and I could not. Job 92159466139 on ubuntu-24.04 with apt Qt 6.4.2 built pptv_ui_tests and ran all 178 tests green in 3.77s, including the nine new 'U:' widget tests (#170-#178). I reproduced the whole thing locally from a clean clone twice (178/178 both times) and additionally forced QT_QPA_PLATFORM=xcb and QT_QPA_PLATFORM=totally_bogus at the binaries to try to make them fail — the in-code qputenv wins, so they run regardless. The offscreen plugin genuinely ships with qt6-base-dev's dependency chain: libqt6gui6t64 and qt6-qpa-plugins both installed under --no-install-recommends, visible in the run log. No YAML fix is required for CI to be green today; finding 2 is about fragility, not a current break.
- clang-analyzer on the new code. The path-sensitive analyzer (clang-analyzer-*) found ZERO issues across all 11 files in src/present/ and src/ui/. So did bugprone-* and cert-*. I then went beyond the requested set and added concurrency-* (the obvious place to look, given TM-018's off-thread workers) plus misc-*, cppcoreguidelines-pro-type-member-init, -slicing and -init-variables: still zero concurrency findings and zero uninitialised-member findings. The QThread/QObject::moveToThread/QImage-not-QPixmap discipline holds up under static analysis. I want to be clear this is a real negative result, not a check I skipped.
- git-LFS as a build or CI dependency. Nothing in CMakeLists.txt, tests/CMakeLists.txt, tests/ui/CMakeLists.txt or src/ references vosk — the only mentions are a comment and a doc comment in recognizer_controller.hpp. I built two independent clones with the LFS objects deliberately absent: one via GIT_LFS_SKIP_SMUDGE=1, one with the lfs filter config neutered to simulate a machine with no git-lfs installed at all. Both cloned cleanly (10 MB of pointer text, git status clean — no phantom modifications), configured, built, and passed 178/178. CI checks out with lfs: false (confirmed in the runner log), so it never downloads the 76 MB and does not need to. This will change the day Vosk is actually linked; it is a non-issue for F7b and for the talk.
- scripts/run-tests.sh, the commit gate. Ran it from a fresh clone with only the documented environment: exit 0, clang-format check clean, 178/178. It still does what it claims. Its one gap is that it runs clang-format but never clang-tidy, which is why finding 5 (the macOS sysroot problem) is latent rather than active.
- Deck-content and deck-path leakage through the CLI/launch path. I ran the built app against a good fixture, against bad_notzip.pptx, and against a nonexistent path I named /tmp/CONFIDENTIAL-Q3-BOARD-DECK.pptx specifically to see whether it would be echoed. It never appeared in stdout or stderr in any run. The only output was Qt's own font-database notice and offscreen-QPA plugin notices, neither of which carries deck data. The fixed-string QMessageBox path (describeLoadError by kind) holds. I did not open, read, copy or render the real Confidential deck at any point.
- macOS Finder / odd-argv launch handling. I expected the legacy '-psn_0_xxxxx' argument that macOS can pass to a bundled app to blow up QCommandLineParser::process() and abort before the window ever appears — it does not, Qt strips it and the app starts normally. Unknown options exit cleanly with rc=1 and a single-line message rather than hanging or crashing.
- AppShell's fullscreen / external-projector path (honest non-result rather than a clean bill of health). src/ui/app_shell.cpp:194-208 picks the first non-primary QScreen and calls setScreen/setGeometry/showFullScreen/raise/activateWindow, and moveWindowToNextScreen at :251 is the Ctrl+Shift+D escape hatch. I could not test any of it: it needs a real window server and a second physical display, offscreen QPA explicitly refuses raise() and propagateSizeHints(), and I deliberately did not launch a fullscreen, Esc-resistant window on Karl's own machine unsupervised. So I am not claiming it works — I am recording that no automated test on any platform covers it and neither did I. It needs a hands-on rehearsal against the actual projector before 2026-08-10.
