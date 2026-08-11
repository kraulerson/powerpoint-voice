# Senior Software Engineer Review — powerpoint-voice

**Reviewer:** independent senior engineer (Phase 3 evaluation, reviewer 01 of 6)
**Date:** 2026-08-10
**Tree reviewed:** `main` @ `9c6eb80`. **The working tree changed under me during this review** — parallel Phase 3 reviewers edited `USER_GUIDE.md`, `SECURITY.md` and `BUGS.md` (adding BUG-88 and BUG-89) while I was reading. All `src/` citations are stable; all `USER_GUIDE.md` citations below are against the **current working tree**, not `9c6eb80`, and are re-verified as of writing.
**Scope:** full read of `src/` (4,758 lines C++), `tests/` (5,997 lines), `CMakeLists.txt`, `cmake/`, `.github/workflows/`, `scripts/`, and the 13-document doc corpus.
**Verification performed:** built the tree and ran `bash scripts/run-tests.sh` (297/297 pass), then ran `ctest` a further 8 times (9 clean runs total). Read `docs/test-results/`, `BUGS.md`, `WALK-ISSUE-LOG.md`, and every UAT session's agent results. Did **not** open the Confidential deck; every deck-specific claim below is quoted from the project's own records.

---

## Executive summary

This is the best-commented, most honestly-documented codebase of its size I have reviewed, and the layering is real rather than aspirational — all decision logic is pure and headless, which is the only reason 297 tests can run on a machine with no microphone, no projector, and no second display. It is also a codebase whose test suite has not found a single one of its last nine defects: BUG-79 through BUG-87 were found eight-by-adversarial-review and one-by-CI, zero by tests, and I found ten more of the same class in a day. Two matter most. **The load-warning list that `PRODUCT_MANIFESTO.md:37` makes an acceptance criterion of feature F1 is computed at 10 sites in the loader and read by nothing in the entire product** — the "never a silent wrong render" contract is half-implemented, and an independent UAT-4 reviewer already proved the other half leaks (shape fills discarded, measured at 55 invisible characters on slide 5 of the actual talk deck), a CONFIRMED SEV-2 that never reached `BUGS.md` and is still live today. And **the "Paused — voice control is off" banner does not persist**: any keyboard navigation overwrites it, while `USER_GUIDE.md:96-97` promises it "stays there" and `USER_GUIDE.md:175-177` — added *today*, as the remediation for BUG-88 — makes its disappearance the presenter's signal that an audience phrase has un-paused them, with "press **P**" as the response. Press P in that state and you resume a session that was still paused, during Q&A. As an engineering artifact this is a 4; as a distributable desktop product — no installer, no notarisation, no updater, a release workflow that cannot execute, and CI that never builds the only platform it ships on — it is a 2.

---

## Phase 1 — Inventory

**What it claims to do.** Render a `.pptx` fullscreen on an external display and drive it with five spoken commands, entirely offline, with the keyboard as a guaranteed fallback (`PRODUCT_MANIFESTO.md:37`, `USER_GUIDE.md:3-4`).

**What it actually does.** All of that. `main.cpp` → `AppShell` → off-thread `DeckLoadWorker` (libzip + pugixml → `Presentation`) → off-thread `PreRenderWorker` (`SlideRenderer` → `QImage` per slide) → `PresentationWindow`/`SlideSurface`. In parallel, `MiniaudioCapture` (CoreAudio) → `audio_format` conversion → `VoskEngine` (grammar-constrained) → `RecognizerController` gate → `PresentationController`. The renderer covers solid backgrounds (with slide→layout→master inheritance), text runs with per-run font/size/weight/colour, images (PNG/JPEG only) with `srcRect` cropping, `stretch`, and `alphaModFix`, plus labelled placeholder boxes for tables/charts/SmartArt.

**Dependency chain.** Qt 6 (Core/Gui/Widgets/Test, LGPL-3.0, Homebrew), libzip + pugixml (pkg-config, Homebrew), Vosk 0.3.44 + `vosk-model-small-en-us-0.15` (vendored, git-LFS, SHA-256 pinned), miniaudio 0.11.25 (vendored single header), doctest 2.4.11 (FetchContent, exact tag).

**Architectural patterns.** Two static libraries with a hard dependency boundary: `pptv_core` links `Qt6::Gui` only; `pptv_ui` adds `Qt6::Widgets`. Policy/mechanism split, dependency injection at every hardware boundary (`IAudioCapture`, `DeckLoadWorker::LoadFn`, `PreRenderWorker::RenderFn`), pure functions for every decision that can be one.

**QA mechanisms.** 297 doctest cases across two binaries, `clang-format --Werror` at commit and in CI, `scripts/lint-test-names.sh` (fails closed on ghost test registrations), gitleaks over full history, Semgrep, ASan/UBSan/TSan build trees, five UAT sessions, ten security-audit documents, seven Phase 3 audit documents.

---

## Phase 2 — Structured review

### 1. Architectural Soundness

**Assessment.** The `pptv_core` / `pptv_ui` split (`CMakeLists.txt:68-127`) is the load-bearing decision and it is correct. Every decision lives on the core side as a pure function or a headless class: `PresentationController` (the single funnel for every slide-index computation, both input paths), `key_translator`, `display_geometry`, `raster_cache`, `command_matcher`, `number_parser`, `audio_format`, `notice`. The widgets paint and forward; `SlideSurface::paintEvent` asks `fitRect()` where to draw rather than deciding. Pause state has exactly one owner (`RecognizerController`) and `PresentationController` deliberately refuses to hold a second copy (`presentation_controller.hpp:14-19`) — the comment explains that a second pause bit is how BUG-11 was resurrected in design review. Quit is reachable only through `confirmQuit()`, which is reachable only from `Mode::ConfirmQuit`, so no `Command` can end a talk.

**Strengths.** The testability argument is not theoretical: the development machine has no microphone and the talk runs on different hardware, and the architecture is shaped by that constraint rather than in spite of it. Directory structure maps one-to-one onto the pipeline (`loader/`, `render/`, `command/`, `audio/`, `present/`, `ui/`) and is navigable in minutes. Worker lifetimes are handled with `QPointer` throughout because `deleteLater` on `QThread::finished` genuinely dangles raw pointers (`app_shell.hpp:133-142`), and a generation counter (`app_shell.cpp:509-520`) stops an abandoned worker's rasters landing in the next deck's slots.

**Weaknesses.** The domain model is the ceiling. `ShapeElement` (`slide_model.hpp:106-111`) is a tagged struct carrying all three payloads inline rather than a variant, and `TextBox` (`slide_model.hpp:51-54`) is `{rect, paragraphs}` with no fill, no alignment, no anchor, no rotation. Every rendering feature the product does not have is blocked behind widening that struct and the two functions that consume it. `AppShell` at 649 lines is the one place the layering thins — it owns worker lifetime, voice lifetime, screen selection, raster eviction, and the notice string, and eight public `...ForTest` members (`app_shell.hpp:34-76`) are compiled into the shipping binary because the class could not otherwise be driven.

**Gap analysis.** No `LoadReportView` — one of the four views `PROJECT_BIBLE.md:222-223` specifies. No operator surface, so `NoticeRole::Operator` exists with one caller that passes it for a notice `noticeForRole` ignores the role for (`app_shell.cpp:558`). Two `UiRequest` values (`ToggleFullScreen`, `ReRenderDeck`) have producers and no consumer.

**Verdict: 4/5.**

---

### 2. Code Quality and Consistency

**Assessment.** Style is uniform and machine-enforced (`.clang-format`, checked in `run-tests.sh:20-24` and CI). Naming is consistent (`pptv::` namespace, `lowerCamel` functions, `trailing_` members). The comments are the standout: nearly every non-obvious line carries the measurement, crash report, or counter-example that produced it — `miniaudio_capture.cpp:70-112` reads the vendored miniaudio source line-by-line to prove `ma_device_stop()` does not join the audio callback; `slide_renderer.cpp:200-213` explains why summing two ints in a guard was UB in the guard's own inputs; `key_translator.cpp:36-54` records the measured auto-repeat rate that made a held P key land back on live. This is what makes the codebase maintainable.

**Strengths.** Error handling is deliberate and consistent: a closed `enum` per failure domain (`LoadErrorKind`, `CaptureError`, `RecognizerInitError`), each with a `describe*()` returning a fixed string, and `LoadError::message` is structurally prevented from reaching a widget (`deck_load_worker.hpp:37-41`). Nothing is swallowed silently at the error-reporting layer.

**Weaknesses.**

- **No warning flags anywhere.** `grep -n "Wall\|Wextra\|Werror\|Wconversion" CMakeLists.txt tests/CMakeLists.txt cmake/*.cmake` returns nothing. 4,758 lines of C++ full of hand-audited integer narrowing, signed-overflow guards, and `static_cast`s, compiled with default diagnostics only.
- **clang-tidy has never run.** `CMakeLists.txt:16` exports `compile_commands.json` "for clang-tidy"; `run-tests.sh:11-13` puts LLVM on `PATH` for it; `.github/workflows/ci.yml` gates the step on `hashFiles('.clang-tidy')` — and **there is no `.clang-tidy` file in the tree**. The project logged this itself as OBSERVATION-016; it is still true, and `PROJECT_BIBLE.md` §10 documents clang-tidy as enforced.
- **Dead code that reads as live feature.** `NoticeClass` is constructed at 13 sites and read at 0 (BUG-29, deferred) — `notice.hpp:28` documents "Transient notices fade" and nothing fades. `SurfaceState::HoldLastGood` is returned by a tested pure function with no production caller. `voiceUnavailableReason_` is written once and its only reader is an inline getter with zero call sites. `Presentation::warnings` is written at 10 sites and read nowhere (see §5). Every one of these is a promise in the type system that the product does not keep.
- **Test seams in production.** `AppShell` exposes `openDeckGenerationForTest`, `deliverSlideForTest`, `installVoiceForTest`, `installWindowSinksForTest`, `controllerForTest`, `hasRasterForTest`, `deckGenerationForTest`, `voiceGatePausedForTest` as public API of the shipping library.

**Gap analysis.** No static analysis of any kind reaches the C++. The Semgrep configuration (`ci.yml`, sast job) is `p/owasp-top-ten` plus two JavaScript/DOM rulesets; the archived Phase 3 run (`docs/test-results/phase3/semgrep-full-tree-2026-08-10T20-53-27Z.json`) scanned 29 `.cpp` and 27 `.hpp` files with `--config auto` and returned 0 findings. A zero from a scanner with near-zero C++ rule coverage is not evidence.

**Verdict: 4/5** — the craft is a 5, the absence of the cheapest available machine checks pulls it down.

---

### 3. Dependency Management

**Assessment.** Seven components, all pinned. `third_party/PROVENANCE.md` records the source URL, upstream digest, and committed-file SHA-256 for each vendored artifact, with the wheel→library extraction chain documented. `sbom.json` is a valid CycloneDX 1.5 document. `doctest` is pinned by exact tag (`CMakeLists.txt:50`). Vosk is pinned at 0.3.44 with the deviation from ADR-0001's 0.3.45 explained (0.3.45 ships no macOS binary) and the header's provenance from the 0.3.45 tag ABI-verified with `nm -gU`.

**Strengths.** The CVE handling is exemplary and is the model I would want other teams to copy. CVE-2026-32837 (miniaudio ≤0.11.25, heap OOB read in WAV BEXT parsing) matches the pinned version. `docs/test-results/2026-08-10_dependency-vulnerability-review.md` does not argue it away — it proves unreachability three independent ways (the guards that compile the decoder out, `nm -a` showing zero `dr_wav` symbols in the object *and* the shipped binary, and the structural argument that no code path opens an audio file), and then records the one-line change that would make it live. That is how a vendored CVE should be dispositioned.

**Weaknesses.** The three dependencies that actually ship — Qt 6.11.1, libzip 1.11.4, pugixml 1.16 — come from Homebrew and are pinned by **nothing in the build**. `pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip)` (`CMakeLists.txt:43-44`) accepts whatever is installed. `sbom.json` and `THIRD_PARTY_NOTICES.md` therefore record the versions on one machine on one day, not a reproducible input. The consequence is already visible: `scripts/make-test-build.sh` computes the bundle's macOS floor from whatever Homebrew built against, and `WALK-STATE.md:63-65` records that floor as macOS 26 — a runtime requirement set accidentally by the build host. `PROJECT_BIBLE.md:50`, `ADR-0001:32` and `threat-model.md:310` all say pugixml **1.15** (the last of those makes the version load-bearing for a security invariant, "an ADR is required to change the XML parser"); the SBOM says 1.16 and no ADR records the bump.

**Gap analysis.** Nothing watches for new CVEs — the vulnerability review says so itself. `osv-scanner` is configured in CI but skipped, because it requires a `vcpkg.json`/`conan.lock` the project does not have; a `vcpkg.json` would both pin the three unpinned libraries and turn that CI step on. No dependency could be replaced with the standard library: libzip, pugixml, Vosk, and miniaudio are all doing real work, and nothing heavy is pulled in for a trivial task.

**Verdict: 4/5.**

---

### 4. Testing and Quality Assurance

**Assessment.** I ran the suite. `bash scripts/run-tests.sh` → format check, build, `lint-test-names.sh`, `ctest`: **297/297 pass in 12.5 s**. I then ran `ctest` 8 more times: 9 clean runs total, no flake observed. (BUG-40 records an undiagnosed ~2.5 % residual flake in the worker-thread group; 9 runs is not enough to contradict that and I do not.)

**Strengths.** These are behavioural tests, not coverage theatre. `tests/test_voice_pipeline.cpp:64-101` implements a `ThreadedFakeCapture` that drives the sink from its own thread and whose `stop()` genuinely joins, because that is the contract the real `MiniaudioCapture` provides and the property under test is shutdown ordering. `tests/test_deck_loader.cpp` (38 cases) drives 42 committed fixtures including five deliberate attack archives. `test_widgets.cpp` (31 cases) runs a second binary with its own `QApplication` so the widget layer is exercised for real. Mutation testing appears in the record repeatedly, and where it was found to be fake the project says so.

`scripts/lint-test-names.sh` deserves specific credit. A `;` in a doctest name is split by CMake's list separator, ctest invokes a fragment, doctest runs 0 cases and exits 0, and ctest prints "Passed" — **seven tests were in that state behind a 100 %-green suite** (BUG-76), including the regression test for BUG-71 which had never once executed. The guard that now catches it fails **closed**, counts rather than compares name sets, discovers binaries from ctest's own command lines rather than a filename convention, and is wired into both the commit gate and CI. Its first version had three holes and was itself rewritten after adversarial review (BUG-84). This is a real systemic control and it is the strongest QA artifact in the repository.

**Weaknesses.** The suite does not find this project's defects. BUG-79 through BUG-87: eight found by adversarial PR review, one by CI, **zero by tests**. Three of those (79, 80, 82) had passing regression tests written for the very fix that was broken — `WALK-ISSUE-LOG.md:1033-1037` records a reviewer deleting the *entire* BUG-72 fix and leaving all 279 tests green, because the regression test pinned a property of a test double. The five defects I raise in §5 and below were all found by reading, in a day, and none of them is subtle. The suite is excellent at pinning behaviour that has already been reasoned about and structurally blind to the class this project actually produces: **wiring, ordering, and platform**.

**Gap analysis.**
- **No coverage measurement of any kind.** No gcov/lcov/llvm-cov target, no percentage reported anywhere, against a documented ">80 %" obligation (`PROJECT_BIBLE.md:298-299`).
- **No fuzzing.** `threat-model.md:640-642` mandates libFuzzer harnesses over the three parsing entry points "run in CI"; `PROJECT_BIBLE.md:164` concedes there is none. For a project whose entire input surface is an attacker-supplied ZIP of XML, this is the largest testing gap.
- **CI never builds macOS.** `ci.yml` has one job on `ubuntu-latest`. Every macOS-specific path is verified only on the author's workstation — and the accessibility announcement is *compiled out* on CI's Qt (`a11y_announce.cpp` version guard), so the tests covering it are dead on the only machine that runs them automatically.
- **Sanitizers are not CI jobs.** `build-asan/` and `build-tsan/` exist on disk; nothing runs them on a schedule or a PR.

**Verdict: 3/5.** 297 high-quality tests, a real ghost-test control, and demonstrated inability to catch the defect class that keeps shipping. A 4 would be generous in a way this project's own logs specifically warn against.

---

### 5. Documentation Accuracy

This is the weakest category and it contains the review's most important finding.

#### 5a. The load-warning list does not exist in the product

`PRODUCT_MANIFESTO.md:37` states F1's acceptance criterion: *"unsupported element → visible placeholder + triage-shaped load-time warning list (never a silent wrong render)"*. `PROJECT_BIBLE.md:222-223` names `LoadReportView` as one of four core views.

The loader populates warnings at 10 sites (`deck_loader.cpp`, `slide.warnings.push_back` / `pres.warnings.push_back`). Outside `slide_model.hpp:133,144`, **`warnings` is referenced nowhere in `src/`**. There is no `LoadReportView`, no notice path, no dialog. Every warning the loader carefully produces — shape-cap truncation, unreadable crop, unreadable opacity, unsupported background fill, missing slide part, group-depth truncation, invalid slide size, table/chart/SmartArt — is written into a vector that is destroyed unread.

`FEATURES.md:13` records F1a as **Complete**; the Phase 2→3 gate passed. This is the project's own BUG-60 failure mode (a state that is named but nothing wires) reproduced at whole-feature scale, and the tests could never have caught it because they assert warnings exist in the model, which they do.

#### 5b. The Paused banner is not durable, and the guide's newest safety procedure depends on it

`USER_GUIDE.md:96-97`: *"**'Paused — voice control is off' appears on screen and stays there** until you resume, so you can check at a glance before taking questions."* Two lines later, `:98`: *"Arrow keys keep working while paused."* Both are individually true. Together they are false.

`applyResult` (`app_shell.cpp:642`) recomputes `lastNotice_` on **every** dispatch and `refresh()` (`:578`) re-shows whatever that string now is. Nothing re-asserts the Paused notice, and `NoticeClass::Sticky` is read by nothing (§2). So one arrow press while paused replaces "Paused — voice control is off" with "Slide N", permanently, while the gate is still Paused.

This matters far more than it did this morning. The remediation for BUG-88 — filed and written today, by a parallel reviewer, and correct in its analysis — added `USER_GUIDE.md:175-177`:

> **What to do about it:** glance at the notice strip. If "Paused — voice control is off" has disappeared and you did not resume, press **P**.

That makes the banner's disappearance the presenter's detector for the project's **highest measured residual risk** (an audience phrase in the un-pause family, which fires for 2-3 of 3 voices). The detector produces a false positive on every keyboard navigation — and pausing then navigating to a back-up slide to answer a question is the ordinary way this feature is used. Worse, the prescribed response is wrong in that state: with the gate still Paused, `PresentationWindow::paused_` is still `true`, so `key_translator` maps P to `ContinuePresentation` (`key_translator.cpp:160-164`) and `AppShell`'s sink calls `voiceGate_->setPaused(false)` (`app_shell.cpp:237-238`). **Following the guide takes the presenter from paused to live, during Q&A.** The notice does then read "Resumed", so it is recoverable — by pressing P a second time — but the documented procedure inverts its own intent.

#### 5c. The voice-off message (BUG-89, filed concurrently, still Open in code)

`USER_GUIDE.md` previously stated in three places that a message appears when voice fails to start. `AppShell::armVoice()`'s failure reason is stored at `app_shell.cpp:503` into `voiceUnavailableReason_`, whose only reader is the inline getter at `app_shell.hpp:169` — zero call sites, not a slot, not a property, not `Q_INVOKABLE`. A denied microphone produces **no message at all**. The project's own UAT-5 reviewer documented this on 2026-08-06 (`tests/uat/sessions/2026-08-06-session-5/agent-results/08-verify-presenter.md:191`) and it did not reach the tracker; a parallel reviewer re-found it today, filed it as **BUG-89 (SEV-2, Open — post-talk)**, and corrected the guide to say so explicitly (`USER_GUIDE.md:33-36,45-47,201`). That is the right handling and the doc is now accurate. **The code defect is unchanged**, and it is worth noting how long the loop was: found 2026-08-06, dropped, re-found 2026-08-10 — four days during which the shipped user guide told the presenter to check for a message that cannot exist.

#### 5d. FEATURES.md is stale in four places, including one claim that is now false

- `FEATURES.md:55-56`: *"A text box with mixed-format runs renders with the first run's font."* **False.** `slide_renderer.cpp:141-143` builds a `QTextLayout::FormatRange` per run with that run's font and colour. This limitation was fixed and the note left behind.
- `FEATURES.md:139-141`: *"the TM-018 caps count shapes and text runs only (BUG-21) and the raster cache is unbounded (BUG-22) — both land in F7c."* Both landed; the four-cap set is in `pre_render_worker.hpp:42-49` and the 2 GB window in `raster_cache.hpp:21`.
- `FEATURES.md:178`: *"Not yet wired to a recogniser — that is F8c."* F8c and F8d both shipped (`.claude/build-progress.json`).
- **No F8c, F6, or F7c sections exist at all**, and F5/F6's scope cut (`CHANGELOG.md:132-135`) is not reflected here or in `PROJECT_BIBLE.md:18,304`.

#### 5e. PROJECT_BIBLE contradicts itself

Within one file: a schema-versioned `settings.json` with six keys and a migration path (`:182-200`) versus *"There is no settings file"* (`:148`); a `TranscriptOverlay` core component with four states (`:227-230`) versus *"There is no transcript overlay"* (`:155`); spdlog structured logging "from day 1" writing to `~/Library/Logs/` (`:52,210-215`) versus TM-011 VERIFIED, *"no file is opened for writing anywhere in `src/`"* (`:154`); TM-018's "hard per-slide deadline" (`:105-106`) versus amendment A3-1(2) in the same file explaining a deadline is not implementable (`:78-87`); macOS 14+ (`:311`) versus `USER_GUIDE.md:23`'s macOS 26. `ADR-0001` (Status: Accepted, never superseded) still names Qt 6.8 LTS and Vosk 0.3.45, neither of which any machine in this project has used. `RELEASE_NOTES.md` is an unfilled template.

**Strengths — and they are real.** `USER_GUIDE.md` is, in intent and register, the best user-facing document I have seen from a solo project: it publishes the measured false-trigger rate (60 of 102 near-miss fragments, 0 of 6 natural sentences) *to the user*, in the user's language, with the mitigation. `SECURITY.md`, `PRIVACY_POLICY.md`, and the Phase 3 audits are similarly honest — the accessibility audit names "no end-to-end VoiceOver session has been run" as *"the single largest gap in this audit"* rather than burying it. `third_party/PROVENANCE.md` is exemplary. The problem is not candour; it is that nothing reconciles documents against code, so true statements decay into false ones and nobody notices.

**Would a developer be disappointed?** A user, no — the guide undersells if anything, except on the two indicators above. A maintainer, yes: they would implement against a Bible describing a settings file, a transcript overlay, a log, and a load-report view, none of which exist.

**Verdict: 2/5.**

---

### 6. Error Handling and Resilience

**Assessment.** Every non-GUI thread has an exception boundary, each with a comment explaining why an escape would be fatal: the deck-load slot (`deck_load_worker.cpp:62-83`), the per-slide render (`pre_render_worker.cpp:92-117`) and the render loop separately, and the real-time audio callback at two levels (`voice_pipeline.cpp:48-66` and again at the C-ABI frame in `miniaudio_capture.cpp:145-154`). `PreRenderWorker::renderOne` will never emit a null raster — placeholder, then a 1×1 black floor. Shutdown is bounded (`app_shell.cpp:280-349`) with a documented, *measured* decision to `_exit(0)` on the quit path rather than race `~QGuiApplication` against a live render thread (8 of 14 runs SEGV'd on the tempting alternative; `_exit` was clean 6 of 6). Every capture error is recoverable by construction, and `captureErrorIsRecoverable` exists specifically to make that a stated property with a test rather than a scattered assumption.

**Weaknesses.**
- **Failure modes are not discoverable by the user** because the warning list is never shown (§5a) and the voice-failure reason is never shown (§5b).
- **The malformed/missing asymmetry is backwards.** A missing slide part becomes a numbered placeholder so "go to slide N" cannot drift (`deck_loader.cpp:1020-1023`) — exactly right. A *malformed* slide part aborts the entire load (`:1096-1100`), so one corrupt part in a 60-slide deck yields "That deck is damaged" and no presentation. For a live-presentation tool the more damaging outcome is reserved for the more likely defect.
- **An undecodable-but-present image produces a placeholder with no warning.** The loader cannot know (it never decodes) and the renderer has no warning channel. UAT-4's reviewer flagged this precisely: *"F1's contract is 'visible placeholder + triage-shaped warning list'; only half of it is met."*

**Gap analysis.** No recovery for an evicted raster (§7). No crash reporting by design (correct for the confidentiality posture, but it means a field failure leaves nothing).

**Verdict: 4/5.**

---

### 7. Performance and Scalability

**Assessment.** Measured, not assumed. `docs/test-results/2026-08-10_performance-audit.md` reports parse at 1.5 ms mean, whole-deck 4K rasterisation at 298 ms (worst single slide 194 ms), 316 MB resident at 4K, and the Vosk `feed()` hot path at **0.094 ms mean against a 20 ms budget** — the only hard real-time constraint in the system, correctly identified and measured over 1,500 buffers. Cap headroom on the real deck is two to three orders of magnitude on all four axes.

**Strengths.** `renderOrder` starts at the slide being shown and works outward, so presenting begins immediately. `PreRenderWorker` re-plans after every slide so a mid-render jump re-steers. The TM-018 PREVENT mechanism measures complexity from the *model* in microseconds, so a render bomb never reaches QPainter — the right design, because you cannot abort a paint in flight. Media parts are cached by name (`deck_loader.cpp:1113-1128`) after a measured 2.4 GB amplification.

**Weakness — a new finding.** **Raster eviction creates a slide that can never be displayed again.** `AppShell::onSlideReady` evicts to a 2 GB budget (`app_shell.cpp:529-538`). `PreRenderWorker::done_` guarantees each slide is rendered at most once (`pre_render_worker.cpp:170-180`), and the worker is `deleteLater`'d when it finishes. `showSlide` (`app_shell.cpp:611-623`) only reads `rasters_`; there is no on-demand render path anywhere. Navigating back to an evicted slide therefore shows **"Rendering slide N..." permanently**. The comment at `pre_render_worker.cpp:191-192` asserts the opposite — *"every slide already emitted stays usable, and `showSlide()` renders on demand for the rest"* — and that sentence is simply false. At ~33 MB per 4K slide the budget holds ~63 slides against a 300-slide cap, so this is unreachable on the 10-slide talk deck and a certainty on the arbitrary deck the budget was introduced for. The performance audit analyses the budget at length and does not analyse this.

**Other gaps.** BUG-33 ("slides render a bit slowly on the real deck") is **Open** and has never been re-measured natively at projector resolution; the leading suspect in its own row is ~50 missing font families, which connects to §8 below. `SurfaceState::HoldLastGood` — the mechanism that stops a jump flashing the projector black — is implemented, tested, and never called.

**Verdict: 4/5.**

---

### 8. Platform Integration

**Assessment.** The macOS-specific work that exists is careful and hard-won. `NSMicrophoneUsageDescription` is supplied via a custom `Info.plist` template because CMake's default omits it and macOS *terminates* a process that opens an input device without it. `libvosk` is given an `@rpath` install name and re-signed at build time because `install_name_tool` invalidates the ad-hoc signature and an invalid signature is a hard load failure on arm64 with a different error. Quit semantics are worked out properly: `QEvent::Quit` is distinguished from a window close so Dock→Quit and Activity Monitor→Quit are obeyed while a stray Esc is not, and `quitConfirmChord()` returns **Cmd+Q rather than Cmd+Shift+Q** because the latter is the system Log Out shortcut and printing it on a projector invites a presenter to log the machine out mid-talk. The fullscreen window is placed on the non-primary screen by default. Accessibility uses `QAccessibleAnnouncementEvent` — the *only* Qt event the Cocoa plugin can map to an AppKit notification carrying a message, determined by disassembling `libqcocoa.dylib`.

**Weaknesses.**
- **`NSHighResolutionCapable` is set but `LSMinimumSystemVersion` is not**, despite `USER_GUIDE.md:23` documenting a macOS 26 floor that `WALK-STATE.md:63-65` confirms is real. On an older macOS the app will launch and fail at dyld with an unhelpful error instead of being refused cleanly.
- **No `CFBundleDocumentTypes`** — no `.pptx` association, no Open With, no double-click-a-deck. **No `CFBundleIconFile`** — a generic icon in the Dock during a talk.
- **`QApplication::setDesktopSettingsAware(false)` (`main.cpp:24`)** is a legitimate fix for a real data race, but it is process-wide: the `QFileDialog` and `QMessageBox` the app does show no longer follow system appearance or display accessibility settings. The comment justifies it on the presentation surface only.
- **Two advertised chords are dead.** `key_translator.cpp:89-94` produces `ToggleFullScreen` (Ctrl+Shift+F) and `ReRenderDeck` (Ctrl+Shift+R) and sets `consumed = true`; `app_shell.cpp:265` drops both into `default: break`. The keys are swallowed and do nothing, which is worse than not binding them. Found in UAT-3 (2026-08-05), folded into BUG-29, deferred.
- **No end-to-end VoiceOver session has ever been run** — the accessibility audit says so itself and calls it the audit's largest gap. Everything below the Qt bridge is unverified.

**Gap analysis — a new finding on rendering fidelity.** `grep` across `src/` for `normAutofit`, `lnSpc`, `algn`, `anchor`, `rot`, `fntdata`, `QRawFont` returns **nothing**. The renderer implements no paragraph alignment, no vertical anchoring, no PowerPoint autofit, no line spacing, no shape rotation, and no embedded fonts — and it does not clip text to its own box (`slide_renderer.cpp:262` sets one clip for the whole slide; `drawTextBox` advances `cursorY` with no bound against `box.bottom()`). On a real corporate template the compound effect is: centred titles render left-aligned; text PowerPoint had auto-shrunk renders at full declared size; and with ~50 declared font families absent on macOS, substituted metrics make overflow *more* likely, with the overflow drawing over whatever is beneath it. `FEATURES.md:55-57` lists two F1b limitations and neither is any of these; `USER_GUIDE.md:179-193` lists EMF/WMF and tables/charts/SmartArt only. `PROJECT_BIBLE.md:40` and `ADR-0001:26-27` both promise embedded fonts via `QRawFont`, while `PROJECT_BIBLE.md:159` claims TM-016 is STRUCTURAL *because* font data is never loaded from a deck — the same decision, recorded as a security win in one place and promised as a feature in another.

**Verdict: 3/5.**

---

### 9. Resource Management

**Assessment.** Memory is bounded at three levels: loader caps (200 MB archive, 1 GB decompressed, 128 MB per part, 300 slides, 5,000 shapes/slide, 2,000 paragraphs/box, 1,000 runs/paragraph, 100k chars/run), render caps (shapes, runs, characters, declared image pixels), and the 2 GB raster window. Media parts are read once and shared via copy-on-write `QByteArray`. Image decode is bounded in dimensions (40 Mpx) and allocation (128 MiB) *before* the decoder runs.

**Strengths.** Cleanup is genuinely thought about. `~AppShell` tears down voice first, before any member is released, because CoreAudio's real-time thread is the only input still arriving and it is calling into objects the destructor is about to free — and the same ordering is encoded a second time in member declaration order, deliberately, so deleting either mechanism leaves the app correct. No temp files, no sockets, no child processes; `TM-011` forbids writing to disk at all and `SECURITY.md:14` states no file is opened for writing anywhere in `src/` (I confirmed this by inspection). Idle CPU is a single 250 ms `QTimer` driving the quit-prompt timeout, plus the audio callback when voice is armed.

**Weaknesses.** A worker whose 5 s wait expires is deliberately detached and leaked (`app_shell.cpp:340-341`) — the right trade against `terminate()` or a `qFatal` abort, correctly reasoned, but it is still an unbounded leak if `openDeck` is called repeatedly. `onSlideReady` rebuilds a full `sizes` vector over all rasters on every completed slide — O(n²) across a deck, irrelevant at n=300 but gratuitous. Two 500 MB+ sanitizer build trees are checked into the working directory (correctly gitignored).

**Verdict: 4/5.**

---

### 10. Installer, Updater, and Lifecycle

**Assessment.** `scripts/make-test-build.sh` is a genuinely careful piece of work and the best packaging artifact here: it runs `macdeployqt`, repairs every copied dylib's `LC_ID_DYLIB` (because macdeployqt does not reliably do so), stages third-party licences **before** signing (because adding to `Contents/Resources` afterwards invalidates the seal — *"Measured, not guessed — that is exactly what the first version of this step did"*), re-signs, verifies the speech model and its dynamic graph are inside the bundle, and **fails the build** if any binary still references `/opt/homebrew` or if any required licence text is missing. It reports the arch and the computed minimum macOS. That script is a 4.

Everything around it is not.

**Weaknesses.**
- **`release.yml` cannot execute.** `- name: Install dependencies / run: # TODO` and `- name: Build / run: # TODO` are empty `run:` values; the SBOM step ends in `exit 1`. It is a template that was never authored for C++ and would fail on every tag push. Tracked as ISSUE-003/010, deferred to Phase 4.
- **No installer.** A `.zip` requiring `right-click → Open`, not the DMG that `PROJECT_BIBLE.md:56,259`, `ADR-0001:33`, and `threat-model.md:20` all specify.
- **No Developer-ID signing, no notarisation, no hardened runtime.** TM-003, TM-022, TM-023 all remain open. `USER_GUIDE.md:18-21` instructs the user to run `xattr -dr com.apple.quarantine` — teaching a Gatekeeper bypass is a poor habit to ship, even to one's own machine.
- **No updater** (documented as manual — acceptable, and correctly declared).
- **No uninstall documentation at all.** `grep -i uninstall` returns zero hits across the entire doc corpus, against an explicit platform-module requirement to document it.
- **No settings migration** because there are no settings — which is fine, except that `PROJECT_BIBLE.md:182-200` still specifies a schema-versioned settings file and its migration path.
- **First-run setup** is handled well: the model is extracted at build time so there is no first-launch unzip in front of an audience.

**Verdict: 2/5.**

---

### 11. Offline and Local-First

**Assessment.** Fully offline by construction, and this is the one claim I could verify structurally rather than by testimony. There is no network code in `src/` of any kind. `localDeckPathFrom` (`start_view.cpp:17-28`) accepts only `url.isLocalFile()`, with the reason stated: accepting a remote URL *"would create the project's first network path by accident, in a drop handler."* The speech model and library are vendored and staged into the bundle, so there is no runtime download. Nothing is written to disk while running — no cache, no log, no transcript, no recent-files list.

**Strengths.** User data portability is trivially perfect: the user's data is their own `.pptx`, opened read-only and never modified. There is nothing to export because there is nothing to lock in.

**Weaknesses.** None material. The only nuance is that the offline invariant is enforced by discipline and review rather than by a build check — `PROJECT_BIBLE.md:247` itself notes "a CI grep guard is a candidate". A `nm`-based check that no network symbol is reachable would make it structural, and would cost about six lines.

**Verdict: 5/5.**

---

### 12. Security (Desktop-Specific)

**Assessment.** The posture is strong and, unusually, mostly *structural* rather than procedural — the attack surface is removed rather than guarded.

**Strengths.**
- **Untrusted-input handling in the loader is the best part of the codebase.** Decompression caps are enforced from the central directory before any part is read; `st.size` is compared **unsigned** so a ZIP64 declared size >2^63 cannot wrap negative past the cap; XML descent is iterative with an explicit heap stack so a nested hostile deck cannot blow the call stack; group recursion is capped at 32; a part name is copied *before* `zip_close` because libzip frees it there and formatting it afterwards was an attacker-triggered use-after-free *and* an information-disclosure channel into a user-facing dialog.
- **Image decode is allow-listed by magic bytes before any Qt decoder is constructed** — deliberately including not calling `QImageReader::format()`, which probes and loads image plugins on whatever thread asks and crashed the app outright on EMF parts. TIFF/WebP/GIF codecs are never reached.
- **The closed notice vocabulary is a genuinely elegant control.** A `Notice` is an id plus two ints; `noticeForRole` has no free-text parameter. Deck content, file paths, and heard speech have **no channel** to a display or a log. `describeLoadError` is the only path from a load failure to a dialog, and it ignores `LoadError::message` entirely.
- **Minimum permissions.** One entitlement (microphone), honestly worded, requested only after the window exists so the prompt cannot land on a bare projector.
- **Grammar enforcement fails closed.** Voice refuses to run if the model has a static graph (which would silently decode the full ~200k vocabulary), if any grammar word is out-of-vocabulary (Vosk drops those silently), or if the `[unk]` escape hatch did not survive the JSON builder — the last checked against the *emitted JSON*, not the input phrases, because the a-z filter would otherwise reduce `[unk]` to `unk` and BUG-65's guard would miss it.
- The safety claim that could not be supported was **withdrawn** (BUG-66) rather than defended, and the residual published: 60 of 102 near-miss fragments still fire, 0 of 6 natural sentences do.

**Weaknesses.**
- **No hardened runtime, no library validation, no App Sandbox, no notarisation** (TM-022/023, Phase 4).
- **No fuzzing** of the three parsing entry points, which the threat model itself mandates. Sanitizers over a fixed 42-fixture corpus is not the same control, and `PROJECT_BIBLE.md:164` says so.
- **Static analysis does not reach the C++** (§2). For a C++ program parsing attacker-controlled input, `-Wall -Wextra -Wconversion -Werror` plus a `.clang-tidy` with `bugprone-*`/`cert-*` is the cheapest unclaimed control in the project. The codebase has hand-fixed at least three integer-UB defects (`deck_loader.cpp:517-527`, `slide_renderer.cpp:206-213`, `presentation_controller.cpp:178-185`) that a `-Wconversion`-class check would have surfaced systematically.
- **TM-002's specified rate limiting (3 per 5 s, ≥700 ms apart) does not exist** — `dispatch()` takes no timestamp and structurally cannot throttle (BUG-20, deferred). The threat model calls it mandatory; the Bible's TM-002 row does not mention its absence.
- **Path handling** is safe by construction: nothing is ever extracted, parts are read by name through `zip_fopen`, and `resolveTarget` normalises within the archive namespace only. No symlink or traversal surface exists.

**Verdict: 4/5.**

---

## New defects found in this review

Ordered by what I would fix first. None of these appears in `BUGS.md` as of writing.

| # | Sev | Finding | Evidence |
|---|---|---|---|
| SE-1 | **SEV-2** | **The Paused banner is not durable, and BUG-88's own remediation now depends on it.** Any keyboard navigation overwrites it while the gate is still Paused; the guide makes its disappearance the un-pause detector and prescribes "press P", which in that state *resumes*. | `app_shell.cpp:642` + `:578`; `key_translator.cpp:160-164`; `app_shell.cpp:237-238`; `USER_GUIDE.md:96-98` vs `:175-177` |
| SE-2 | **SEV-2** | **The load-warning list is populated at 10 sites and read by nothing.** Manifesto F1's acceptance criterion is half-implemented; `LoadReportView` does not exist. | `deck_loader.cpp` (10 `warnings.push_back`) vs `grep -rn warnings src/` → only `slide_model.hpp:133,144` |
| SE-3 | **SEV-2** | **Shape fills are discarded** — a CONFIRMED UAT-4 finding, measured at 55 invisible characters on slide 5 of the real deck, that never reached the tracker and is still live. | `slide_model.hpp:51-54` (no fill field); `deck_loader.cpp:324` (reads `spPr` only via `parseXfrm`); `tests/uat/sessions/2026-08-05-session-4/agent-results/02-real-deck-fidelity.md` finding #2 vs `submissions/uat-4-agent-arm-results.md:24-32` |
| SE-4 | SEV-3 | An evicted raster can never be re-rendered — the slide shows "Rendering slide N..." forever. The code comment claims the opposite. | `app_shell.cpp:529-538`, `:611-623`; `pre_render_worker.cpp:170-180`, and the false comment at `:191-192` |
| SE-5 | SEV-3 | No `-Wall`/`-Wextra`/`-Werror` anywhere, and no `.clang-tidy`, so the CI lint step is permanently skipped. | `CMakeLists.txt` (no warning flags); `ls .clang-tidy` → absent; `ci.yml` clang-tidy step `if: hashFiles('.clang-tidy') != ''` |
| SE-6 | SEV-3 | Text layout implements no alignment, anchoring, autofit, line spacing, rotation, or embedded fonts, and does not clip to the text box. Undocumented; `FEATURES.md:55-56`'s stated limitation is itself now false. | `grep -rn "normAutofit\|lnSpc\|algn\|anchor\|fntdata\|QRawFont" src/` → nothing; `slide_renderer.cpp:86-166`, `:262` |
| SE-7 | SEV-3 | Theme colours and master text styles are hard-coded to `theme1.xml` / `slideMaster1.xml` while backgrounds correctly follow the layout→master chain. A multi-master deck gets wrong scheme colours and wrong inherited font sizes. | `deck_loader.cpp:970,980` vs `:1044-1091` |
| SE-8 | SEV-3 | A malformed slide part fails the entire deck load; a missing one becomes a placeholder. The asymmetry favours the worse outcome. | `deck_loader.cpp:1096-1100` vs `:1020-1023` |
| SE-9 | SEV-4 | `Info.plist` has no `LSMinimumSystemVersion` despite a documented and real macOS 26 floor; no `CFBundleDocumentTypes`, no icon. | `cmake/MacOSXBundleInfo.plist.in`; `USER_GUIDE.md:23`; `WALK-STATE.md:63-65` |
| SE-10 | SEV-4 | Eight public `...ForTest` members ship in the production library. | `app_shell.hpp:34-76` |

BUG-89 (voice-off message never displayed) was filed by a parallel reviewer during this review; I found it independently and it is discussed at §5c rather than listed here.

**A process observation behind SE-2.** UAT-4's independent skeptic confirmed four defects in `02-real-deck-fidelity.md`; the session submission consolidated five bugs to `BUGS.md` (40, 60, 61, 62, 63) and findings #2 (shape fills, SEV-2), #3 (`AlternateContent` dropped), and #3b (`<p:sp>` with no `<p:txBody>` dropped silently, no warning, no placeholder) were never given numbers. `BUGS.md` therefore cannot be used as the index of open work, which matters more than any single one of those defects.

---

## Would I use this?

**Personal projects — yes, without hesitation.** As a tool for one presenter, on one Mac, with a rehearsed deck and a keyboard fallback that genuinely always works, this is better than it needs to be. I would use it on 2026-08-12. I would do the three things in "Before the talk" below first.

**Small team projects — yes, with conditions.** The code is readable by a new engineer in an afternoon precisely because of the comment discipline, and the layering means a second contributor can work in `pptv_core` without touching Qt widgets. But I would require, before a second person commits: `-Wall -Wextra -Werror` and a `.clang-tidy`; a macOS CI job; and one pass reconciling `PROJECT_BIBLE.md`/`FEATURES.md` against the code, because the current documents will actively mislead them about a settings file, a transcript overlay, a log, and a load-report view that do not exist.

**Enterprise projects — no, not as it stands.** Not because of the code, which is better than most enterprise C++ I have reviewed, but because of the distribution and assurance envelope: unsigned and un-notarised, no installer, a release pipeline that cannot execute, no coverage measurement, no fuzzing over an attacker-controlled parser, no static analysis reaching the C++, and CI that never builds the shipping platform. Those are all months-out-of-scope for a solo talk tool and all blocking for enterprise distribution. The security *design* would pass an enterprise review; the security *assurance* would not.

---

## Critical fixes

The five things that must change for this to be taken seriously as production desktop software, in priority order.

1. **Make the Paused state durable, because a safety procedure now depends on it.** `refresh()` should re-derive the strip from live state — `voiceGatePaused()` plus the last transient notice — rather than replaying a flattened `lastNotice_` string, so the Paused banner is re-asserted after every navigation and cannot be silently overwritten. The 250 ms `tick_` timer already running can expire the transient half, which closes BUG-29 in the same change. Until it is fixed, `USER_GUIDE.md:175-177` should not tell the presenter to treat the banner's absence as evidence of an un-pause: today that instruction misfires on ordinary use and its prescribed response resumes voice during Q&A.

2. **Surface the load warnings, or delete them and amend the Manifesto.** F1 is recorded Complete against an acceptance criterion half of which does not exist, and no test can detect that. Minimum viable version: after `onDeckLoaded`, if `deck_->warnings` is non-empty, show a load report listing count-by-type — the vocabulary is already closed (`elementType` is a fixed set), so nothing from the deck leaks. Add a widget test asserting the shell **wires** it, per BUG-60's own lesson.

3. **Close the silent-drop set and re-verify the real deck slide-by-slide.** Add `std::optional<Color> fillColor` to `TextBox`, parse `<a:solidFill>` from `<p:spPr>` in `parseTextBox`, fill before layout in `drawTextBox` (~10 lines, per the UAT-4 reviewer's own patch sketch); emit a `LoadWarning` instead of `continue` when a `<p:sp>` has no `<p:txBody>`; emit a warning for an image that is present but undecodable. Then compare all ten rendered slides against PowerPoint, not "unchanged since last time". Render `voiceUnavailableReason_` while you are in `AppShell` anyway (BUG-89).

4. **Turn on the compiler and the linter, and build the platform you ship.** `target_compile_options(pptv_core PRIVATE -Wall -Wextra -Wconversion -Wshadow)` plus a `.clang-tidy` with `bugprone-*,cert-*,performance-*`; expect a day of cleanup and a real chance of finding a sibling of the three integer-UB defects already fixed by hand. Add a `macos-latest` matrix entry to `ci.yml` — today the shipping platform's audio, bundling, signing, and accessibility paths are verified only on one workstation, and the accessibility tests are compiled out on the runner that does exist.

5. **Make an evicted raster recoverable.** Either keep the render worker alive and clear `done_[i]` on eviction so `setCurrentIndex` re-renders it, or render synchronously on a cache miss in `showSlide`. Until then, a deck larger than ~63 slides at 4K has permanently unreachable slides, and the code comment claiming otherwise should go regardless.

---

## Platform Readiness Matrix

| Capability | macOS | Windows | Linux |
|---|---|---|---|
| Builds | **Yes** — primary, dev + ship | **No** — never attempted; `MA_*` audio backend and bundle logic are `if(APPLE)`-guarded but nothing tests MSVC | **Yes** — `ubuntu-latest` CI, every push |
| Automated tests run | **Not in CI** — local only | No | **Yes** — 297 tests, every push |
| Audio capture | **Yes** — CoreAudio via miniaudio, human-verified on target hardware | Unimplemented/untested (miniaudio supports WASAPI; nothing exercises it) | Compiles; never run with a device |
| Speech model shipped | **Yes** — staged into the bundle, dynamic graph verified at build | No packaging path | Extracted to the build tree only; no packaging path |
| Accessibility announcements | **Yes** — Qt 6.11, `QAccessibleAnnouncementEvent`; **no end-to-end VoiceOver session ever run** | n/a | **Compiled out** — CI's Qt < 6.8; the covering tests are dead there |
| Fullscreen / external display | **Yes** — non-primary screen preferred; Ctrl+Shift+D cycles | Untested | Untested |
| Packaging | **Partial** — ad-hoc-signed `.zip` via `make-test-build.sh`; no DMG | **None** — `windeployqt`/NSIS declared P2 | **None** — `linuxdeploy`/AppImage declared P2 |
| Code signing / notarisation | **No** — ad-hoc only; Gatekeeper needs right-click→Open | n/a | n/a |
| Installer / uninstaller | **No** / **No** (undocumented) | No / No | No / No |
| Auto-update | **No** (manual, declared) | No | No |
| OS conventions | **Partial** — quit semantics and mic entitlement correct; no `LSMinimumSystemVersion`, no doc types, no icon, `setDesktopSettingsAware(false)` process-wide | n/a | n/a |
| Overall readiness | **Ship-to-self ready; not distribution ready** | **Not started** | **Build/CI target only** |

Note: the docs claim "Windows 10+ / Ubuntu 22.04+ secondary" (`PROJECT_BIBLE.md:311-312`). Linux is a *compile* target, not a supported platform; Windows is neither. `USER_GUIDE.md:4,23` is the accurate statement of scope, and the Bible should be corrected to match it.

---

## Overall rating: **3.5 / 5**

**Justification.** Split the artifact and the product and the number is easy to defend.

As **engineering**, this is a 4 and in places a 5. The layering is real and enforced by the link graph. The threading discipline — documented member ordering, an explicit teardown repeating it, exception boundaries on all three non-GUI threads, a generation gate in front of the raster sink — is better than most production desktop code I review. The untrusted-input handling in `deck_loader.cpp` and `slide_renderer.cpp` is genuinely good security engineering, structural where it can be. The offline posture is airtight. And the comment discipline is the single most valuable asset here: a maintainer inheriting this can reconstruct not just what the code does but which measurement forced it, including the counter-examples that constrained each fix. I would hire the person who wrote `miniaudio_capture.cpp:70-112`.

As a **product**, it is a 2. No installer, no signing, no notarisation, no updater, no uninstall story, a release workflow that cannot run, and CI that never builds the only platform it ships on.

The honest composite is 3.5, and two things hold it there rather than higher. First, **the documentation has decayed into unreliability** — not through carelessness about truth, which this project has in abundance, but because nothing reconciles documents against code, so `PROJECT_BIBLE.md` now contradicts itself five times and `USER_GUIDE.md` instructs a presenter to act on two indicators the binary cannot produce. Second, **the verification story has a specific, demonstrated shape of blindness**: 297 well-written tests, nine clean consecutive runs, and not one of the last nine defects found by any of them. Everything real came from adversarial review or from a human in front of the hardware. That is worth stating plainly because this project's own logs record repeated over-confidence from exactly this signal, and my five new findings — each visible from reading, in a day — are the same lesson arriving again.

What raises the number rather than lowers it: when this project has been wrong, it has written down that it was wrong, in the file where the wrong claim lived. `BUGS.md:109` says *"my CHANGELOG and header claim is FALSE."* `WALK-ISSUE-LOG.md:915-918` says *"I wrote it, watched ctest say 267 tests passed, and committed. It had never executed."* `USER_GUIDE.md:153` publishes a 60/102 false-trigger rate to the user. That record is why this review could be specific instead of speculative, and it is a stronger predictor of where this codebase ends up than any of the defects above.

---

## Before the talk (2026-08-12)

Three things, in this order, none of which requires a code change:

1. **Open the deck in the app and in PowerPoint side by side, all ten slides.** SE-2 says shape fills are dropped and an independent reviewer measured 55 characters going invisible on slide 5. BUG-39 says two EMF placeholders should be visible on slide 1 and you report not seeing them — an unexplained discrepancy between the model and what you observe. Both are answered by ten minutes of looking.
2. **Know that the Paused banner disappears the moment you press an arrow key, and that it does not mean you were un-paused.** `USER_GUIDE.md:175-177` currently tells you the opposite, and tells you to press **P** — which, if you were still paused, *resumes* voice (SE-1). Safer rule for Thursday: **press P, read the banner, then navigate.** If the banner is gone and you need to know your state, press P and read the notice — "Resumed" means you were paused (press P again); "Paused — voice control is off" means you were live.
3. **Re-run the 60-second false-trigger check in the actual room** if you get the chance. The measured residual (60/102 isolated fragments, worst case the un-pause family) fires exactly during Q&A, which is the window "pause presentation" exists to protect.

---

*This review made no changes to the repository other than creating this file. The suite was built and run (297/297, 9 consecutive clean runs); the Confidential deck was not opened.*
