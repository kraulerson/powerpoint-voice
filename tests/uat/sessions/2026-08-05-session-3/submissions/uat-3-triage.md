# UAT Session 3 — Consolidated Triage

## Headline

NOT presentable as it stands, but it is close — two fixes, both small and both in files already under the microscope, decide it. The core presenting engine is genuinely solid: 178/178 green, ASan+UBSan clean, the loader survives 27 hostile archive families, the command grammar resists every audience sentence thrown at it, BUG-16 range rejection and the two-step quit are unbreakable, and a full 10-slide talk was driven end-to-end with the projector pixels matching the controller at every step. What is broken is the wiring layer (src/ui/app_shell.cpp) and the display geometry — exactly the code no test constructs. FIX FIRST, in order: (1) SEV-1 the Esc privacy blackout un-blanks itself — AppShell::onSlideReady calls showSlide() with no mode check, so the pre-render worker paints the deck back onto a blanked projector 1-3 s after Esc, and also erases the quit prompt; two testers hit this independently and I confirmed it in the source. The one-line fix is to call refresh() instead of showSlide(). (2) SEV-2 but the highest-probability defect in this entire report: on a Retina MacBook plus a 1080p projector the slide is letterboxed twice and covers only 75% of the projector, with 13% smaller text, for the whole talk — and it is invisible in rehearsal because it only appears once a second screen of a different aspect is attached. I verified all three compounding layers in code and the arithmetic reproduces the tester's measured 1662x936 exactly. Everything from item 3 down is a should-fix, not a blocker. Two pre-flight measurements are mandatory regardless of what gets fixed: time render_preview on the real deck (if any single slide exceeds 5 s, item 3 escalates to a crash dialog at quit), and open the real deck once headlessly (item 4 is a silent instant process death with no dialog and no stderr). DROPPED as instructed and not re-reported: BUG-21 (caps count shapes not work — but the regression tester's measurement of 13.3 s for a legal 1990-shape slide is retained as load-bearing evidence for item 3), BUG-22 (unbounded raster cache; Karl's 10-slide deck is ~316 MB, not a risk), BUG-23 (isPlaceholder/HoldLastGood/const-move/parentless widgets — surfaced again by clang-tidy, same item). Also dropped: C1, C2, C3, C4, C5, H4, H5, M1, M5, L3 and the F7a HIGH-1/HIGH-2 fixes, all independently re-verified as genuinely holding. H2 and H3 were previously recorded as fixed but the regression tester proved they are NOT — those are items 1 and 3 and they lead this list.

## Ranked findings (24)

### 1. [SEV-1] The Esc privacy blackout un-blanks itself: the pre-render worker paints the confidential deck back onto the blanked projector (and erases the quit prompt)

*Sources: presenter-flow (SEV-2, pixel-scraped through the real AppShell) and regression-guard (SEV-1, H3 re-test); mechanism independently confirmed in src/ui/app_shell.cpp during consolidation*

**Repro:** Confirmed by me in source. src/ui/app_shell.cpp:211-220 — onSlideReady() ends with `if (index == controller_.currentIndex0Based()) { showSlide(controller_.currentSlide1Based()); }`. showSlide() (line 270) unconditionally calls window_->setSlideImage(rasters_[i]). refresh() (line 226) is the ONLY function that honours Mode::Holding / Mode::ConfirmQuit, and this path bypasses it entirely.

The plainest stage flow reproduces it with no navigation at all — open the deck, press Esc while the room settles, wait:
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe openblank <scratchpad>/fix/slow12b.pptx
  -> LEAK at t=1150 ms: slide 1 painted itself onto the blanked projector

Pixel-level confirmation from a second, independent harness:
  ./harness/b/presenter_harness "$PWD/decks" s5d
  -> [PASS] Holding  [PASS] blank
  -> lit=0.562 dominant=#0080ff brightSlide=9 mode=Holding
  -> [FAIL] still blank after the renderer caught up
And behind the quit prompt: s5c -> AFTER delivery: mode=ConfirmQuit lit=0.562. Reproduced on the COLD round of a real-shaped 10-slide deck (s5e) in both runs. 56.2% of the projector confirmed as deck pixels while the controller is still in Mode::Holding.

**Impact:** Esc is the only privacy control the product has (TM-002/012/019, Bible section 8). Karl opens his deck at the podium and blanks the projector while the room fills or someone walks in — the single canonical use of the feature — and one to three seconds later the Confidential slide paints itself back onto the wall with no key press, no notice, and nothing on screen to tell him. He is facing the audience, not the screen. The exposure window is exactly the pre-render period, which for a 10-slide deck at ~31.6 MB/slide is roughly 1-5 s after the deck opens: precisely when a presenter blanks the screen. The same path wipes the quit prompt mid-quit, leaving a window that swallows every key while showing a slide — which is H4's 'frozen app' symptom restored. H3 was recorded as fixed on the strength of refresh() alone; the raster path was never gated.

**Fix:** One line in src/ui/app_shell.cpp:onSlideReady — cache the raster as now, then call refresh() (already mode-aware) instead of showSlide(). Belt-and-braces, add an early return to showSlide() itself since it is the single writer of the surface image: `if (controller_.mode() != Mode::Presenting) return;`. Add the regression test that is missing: assert SlideSurface::lastPaintedRect().isEmpty() after a slideReady is delivered while mode == Holding — lastPaintedRect() is already public, so it is cheap. Note that test must not be given a name containing a semicolon (see item 11).

### 2. [SEV-2] On a MacBook plus projector the deck is letterboxed TWICE and covers only 75% of the projector — invisible in rehearsal, certain on stage

*Sources: presenter-flow (sole finder; pixel-measured through the real SlideSurface across three MacBook geometries). Not contradicted — regression-guard checked the adjacent routing question and passed it.*

**Repro:** Confirmed by me in source; three layers compound.
1. src/ui/app_shell.cpp:171 — setTarget(renderTargetPolicy(currentScreens())). display_geometry.cpp:32-51 picks the largest screen by DEVICE pixels across ALL screens, i.e. the Retina laptop (1512x982 at dpr 2 = 3024x1964 = 5.9 Mpx) beats the projector (1920x1080 at dpr 1 = 2.1 Mpx).
2. src/render/slide_renderer.cpp:181-197 — render() returns exactly targetW x targetH with the 16:9 slide letterboxed INSIDE it (img.fill(Qt::black) then a centred slideRect). The bars are baked into the raster.
3. src/ui/slide_surface.cpp:44 — fitRect(image_.size(), ...) fits by the RASTER's aspect (3024/1964 = 1.540), not the deck's (1.778), so the already-boxed raster is boxed AGAIN into the 1920x1080 window.

Arithmetic check: 1080 x 1.540 = 1663 wide; the 16:9 slide inside that is 1663 x 935 — matching the tester's measured 1662x936 to the pixel.

  cat > /tmp/screens.json <<'EOF'
  { "screens": [
    { "name":"laptop","x":0,"y":0,"width":1512,"height":982,"logicalDpi":96,"logicalBaseDpi":96,"dpr":2 },
    { "name":"projector","x":1512,"y":0,"width":1920,"height":1080,"logicalDpi":96,"logicalBaseDpi":96,"dpr":1 } ] }
  EOF
  QT_QPA_PLATFORM="offscreen:configfile=/tmp/screens.json" ./harness/b/presenter_harness "$PWD/decks" s30
  -> surface 1920x1080; slide content occupies 1662x936 at (129,72)
  -> black bars: left=129 right=129 top=72 bottom=72
  -> slide covers 75.0% of the projector
MacBook Pro 16" + 1080p: 75.7%. MacBook Air 13" + 1080p: 74.8%. Laptop alone: correct. Single 1920x1080 screen: 100.0%, no bars.

**Impact:** This is the highest-probability defect in the whole report — firing probability is effectively 1.0 on the actual stage configuration, it lasts the entire talk, and rehearsal cannot reveal it. Karl's 16:9 deck is displayed at 1662x936 on a 1920x1080 projector: a 129 px black frame left and right, 72 px top and bottom, all text 13% smaller than it should be and a quarter of the projector area wasted. It reads to the audience as an app that cannot drive the projector. Severity is SEV-2 rather than SEV-1 only because nothing crashes and nothing leaks — but it should be fixed before item 3 and everything below it. Note the regression tester examined renderTargetPolicy() and concluded the raster was 'correct regardless of which screen the window lands on'; that is true for routing and false for aspect, which is why only the pixel-scraping harness caught it.

**Fix:** Two independent fixes, either of which removes the visible frame; do both. (a) Size the raster from the screen the presentation window is actually on, recomputed on QWindow::screenChanged and on Ctrl+Shift+D, rather than max-over-all-screens. (b) Stop double-fitting: either have SlideRenderer emit a raster at the DECK's aspect with no baked bars and let SlideSurface do the single letterbox, or have SlideSurface fit using the deck aspect it is told rather than the raster aspect — (b) alone fixes it and is the smaller change. Add a geometry test asserting a 16:9 deck on a 16:9 window covers >99% of it regardless of what other screens exist. OPERATIONAL FALLBACK if neither lands: present with the laptop display disabled (lid closed on external power, or the laptop panel turned off in Displays) so the projector is the only QScreen — measured at 100.0% coverage.

### 3. [SEV-2] teardownWorkers() aborts the process (SIGABRT) or deadlocks when a slide render outlives the 5 s wait — the exact qFatal the H2 fix claimed to remove

*Sources: presenter-flow (SEV-1) and regression-guard (SEV-2, H2 re-test, plus the captured deadlock sample); mechanism confirmed in src/ui/app_shell.cpp:71-85 during consolidation*

**Repro:** Confirmed by me in source. src/ui/app_shell.cpp:71-85: `t->quit(); if (!t->wait(5000)) { t->terminate(); t->wait(1000); } t->deleteLater();`. On macOS QThread::terminate() is pthread_cancel and a thread inside a QPainter/CoreText loop has no cancellation point, so it keeps running; deleteLater() then posts a deferred delete that is never processed (teardown runs from ~AppShell after the event loop has exited), and ~QObject destroys a still-running QThread.

  python3 <scratchpad>/mkfix.py /tmp/slow1.pptx bomb 1 1990 2000   # 1 slide, 1990 shapes, UNDER the 2000 cap, 13.3 s to render
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe teardown /tmp/slow1.pptx; echo $?
  -> QThread: Destroyed while thread '' is still running
  -> EXIT STATUS = 134 (SIGABRT), exactly 6.00 s after teardown began = wait(5000)+wait(1000), proving both waits timed out

Second harness, same result: ./harness/b/presenter_harness "$PWD/decks" s23 -> exit=134; s23c prints 'teardownWorkers blocked the UI thread 6015 ms' and 'after wait+terminate+wait: isRunning=1'.

Deadlock variant, captured live with `sample`: main thread in AppShell::openDeck -> QObject::connectImpl -> QBasicMutex::lockInternal -> __ulock_wait2, worker thread in _pthread_exit_if_canceled -> ~PreRenderWorker -> ~QObject -> QBasicMutex::lockInternal. Both on the same orphaned mutex; still wedged after 300 s.

Control: decks that render fast (12 slides at 1.5 s each) exit 0 and print TEARDOWN-DONE.

**Impact:** Reachability re-judged DOWN for this talk: I confirmed AppShell::openDeck has exactly one caller (src/main.cpp:27) and StartView has no open control, so the mid-talk deck-swap trigger presenter-flow found is NOT reachable in v0.1. What remains is the quit path — Esc, Esc, Ctrl+Shift+Q while any single slide is still rendering past 5 s — which produces a hard 6 s UI freeze and then a macOS 'powerpoint_voice quit unexpectedly' dialog. On a 10-slide deck pre-render normally finishes long before the talk ends, so this fires mainly if he quits early (wrong deck, restart). The deadlock variant is the worse tail: it leaves the fullscreen window frozen on the projector with the deck or the blackout still displayed and no way out but Force Quit — the exact C2 failure the audit just closed. This escalates to SEV-1 if any slide in the real deck takes over 5 s to rasterise, which is why the pre-flight measurement below is mandatory.

**Fix:** MANDATORY PRE-FLIGHT (do this regardless): run ./build/render_preview <real deck> <outdir> and divide by 10. If any slide approaches 5 s, treat this as SEV-1.
CODE: do not use terminate() — it is unsafe by construction here. Make wait() always succeed instead. (1) Pass the worker's cancelled_ atomic into SlideRenderer::render and check it once per shape in the element loop, returning a null QImage on cancel — renderOne already handles a null image via the placeholder path, so wait(5000) always returns true and the terminate() branch becomes dead. (2) Delete the QThread directly after a successful wait rather than deleteLater(), which cannot run after the event loop has exited. (3) As a last resort, if wait() ever does fail, DETACH and deliberately leak the thread (t->setParent(nullptr)) rather than destroy it — leaking a thread at process exit is free; destroying a running QThread is a guaranteed qFatal. Add a test that opens a deck whose render blocks on a test-controlled latch, calls teardown, and asserts it returns within a bound with the process alive.

### 4. [SEV-2] Unbounded recursion on nested <p:grpSp> stack-overflows the load worker: a 5.7 KB deck kills the app instantly with no dialog and no stderr

*Sources: hostile-input (sole finder, SEV-1 as reported); recursion and the ineffective cap guard confirmed in src/loader/deck_loader.cpp:436/471-475 during consolidation*

**Repro:** Confirmed by me in source. src/loader/deck_loader.cpp:471-475 — the grpSp branch calls processShapeTree() recursively with nothing bounding depth. The only guard, at line 436, tests slide.elements.size() against lim.maxShapesPerSlide, but a chain of empty <p:grpSp> adds zero elements, so the cap can never fire however deep the nesting goes.

Generator: <scratchpad>/hostile/gen.py. A structurally normal 10-slide deck with only slide 7 poisoned by 1000 nested <p:grpSp>, 5,713 bytes total:
  QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice .../a24_realistic_10slide_poison.pptx
  -> exit=-10 (SIGBUS), wall=0.0s, peak RSS 20 MB, stdout EMPTY, stderr EMPTY

Bisected thresholds prove stack exhaustion conclusively:
  app / load worker thread (512 KB default pthread stack): survives depth 670, dies at 680
  render_preview / main thread (8 MB stack):               survives depth 10,500, dies at 11,000
  ratio 10750/675 = 15.9 ~= 16 = 8 MB / 512 KB, ~760 bytes per frame

Isolation control proves it is the grpSp branch specifically: <a:x> nested 100,000 deep inside <p:bgPr> exits 0 in 0.0s (handled by the ITERATIVE descendantLocal), and tests/fixtures/deep_nest.pptx (depth 3000 of <a:x>) loads fine. Only <p:grpSp> dies.

**Impact:** Severity re-judged DOWN from the reporter's SEV-1 for THIS talk specifically: 680 levels of nested groups is far beyond anything PowerPoint itself produces, and Karl's deck is his own, so realistic probability is near zero. As a product defect against untrusted input it is SEV-1 and should be recorded that way. It stays at SEV-2 here because the failure mode is total and completely silent — the process is gone before onDeckLoaded ever runs, so the QMessageBox at app_shell.cpp:112 is never reached, nothing is printed to stdout or stderr, and relaunching reproduces it every time. It also defeats the entire TM-018 architecture: the crash is in DeckLoader::load on the load worker thread, long before the pre-render PREVENT caps measure anything, and a stack overflow on a secondary thread takes the whole process down. Same bug class as audit F1a-2, which was fixed for descendantLocal and missed on this path.

**Fix:** PRE-FLIGHT: open the real deck once via ./build/render_preview before stage day — a clean exit rules this out entirely for 2026-08-10.
CODE, two cheap options: (a) preferred and consistent with the F1a-2 fix already in the tree, replace the recursion with an explicit heap-allocated work stack, pushing group children instead of calling into itself; (b) minimum viable, thread an `int depth` parameter through processShapeTree and stop descending past a hard limit (64 is far above anything PowerPoint produces), emitting a LoadWarning with elementType "group-depth" so the behaviour is visible rather than silent. Add a fixture at depth 100,000 alongside tests/fixtures/deep_nest.pptx — that fixture only exercises <a:x>, which is exactly why this survived the F1a-2 audit. Do NOT treat QThread::setStackSize() as the fix; it only moves the threshold.

### 5. [SEV-2] A failed open on the CLI launch path leaves NO window and the app exits — the audit-C3 'never leave the presenter with no window' guard is dead code

*Sources: presenter-flow (SEV-3), regression-guard (SEV-4, C2/C3 re-test) and cross-platform (SEV-4, Finder/no-argument launch) — three angles on the same defect; null start_ on the CLI path confirmed in src/main.cpp:25-30 during consolidation*

**Repro:** Confirmed by me in source. src/main.cpp:25-30 calls shell.showStart() only in the `else` branch, so launching with a deck argument — the only way to open a deck — never constructs start_. Both failure paths in src/ui/app_shell.cpp then run `if (start_) start_->show();` against a null pointer and do nothing (lines 114-116 for a load error, 127-129 for a 0-slide deck). With quitOnLastWindowClosed at its default, dismissing the dialog closes the last window and the application quits.

  ./harness/b/presenter_harness "$PWD/decks" s19    # bad path
  -> visible top-level windows after the failure: 0  (start_=NULL window_=NULL)
  ./harness/b/presenter_harness "$PWD/decks" s32    # main.cpp verbatim, real exec() loop
  -> exec() returned after 152 ms; visible windows = 0; dialogs seen = 1
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe cli <scratchpad>/fix/zero.pptx
  -> correct fixed dialog string, then WINDOW present=0, VISIBLE-TOPLEVEL-COUNT 0

And the StartView he would land on is itself a dead end: src/ui/start_view.cpp is setWindowTitle plus two QLabels — no QPushButton, no QFileDialog, no setAcceptDrops. QFileDialog is #included in app_shell.cpp:4 and never used.

**Impact:** Fires whenever a load fails at launch — a mistyped path under stage pressure, the wrong file, a deck PowerPoint saved in a form the loader rejects. Karl clicks OK on the dialog and the application disappears; his only route back is a terminal and the exact CLI incantation, in front of the room. Mid-talk failures are fine and were verified safe (the live window survives and stays navigable), so this is strictly a launch-path risk. Merged from three testers who each found one face of it: the null start_ on the CLI path, the audit text asserting a recovery the shipped path does not provide, and the StartView dead end reachable by double-clicking the .app in Finder.

**Fix:** Cheapest correct change: call shell.showStart() unconditionally in main.cpp before openDeck(), have AppShell::showStart() create start_ lazily (it already does — line 55), and set QApplication::setQuitOnLastWindowClosed(false) while a shell exists. Second, give StartView a single 'Open deck...' button wired to QFileDialog::getOpenFileName -> AppShell::openDeck; the QVBoxLayout is already there, the QFileDialog include is already there, and openDeck is already re-entrant because it calls teardownWorkers() first. If neither lands before 2026-08-10, the mitigation is operational and free: create a .command launcher or shell alias hardcoding the deck path, rehearse launching ONLY that way, and put the exact command in the speaker notes.

### 6. [SEV-3] Notices never expire: NoticeClass::Transient is set on every notice and read by nothing, so a message band sits on the projector for the rest of the talk

*Sources: presenter-flow (SEV-3) and regression-guard (SEV-3) independently; absence of any NoticeClass consumer confirmed by grep during consolidation*

**Repro:** Confirmed by me. `grep -rn 'NoticeClass' src/` returns 12 construction sites (key_translator.cpp:96 and presentation_controller.cpp lines 38-198) and the enum/struct declaration in notice.hpp — and no consumer anywhere. noticeForRole() ignores it, AppShell::applyResult (app_shell.cpp:294) flattens the Notice to a QString in lastNotice_, and refresh() (line 247) re-shows it forever. NoticeStrip has no timer.

  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe notice <scratchpad>/fix/good10.pptx
  -> notice right after a rejected jump: [Deck has 10 slides]
  -> notice 8 s later, no further input:  [Deck has 10 slides]

  ./harness/b/presenter_harness "$PWD/decks" s28
  -> strip right after 'past the end': 'End of deck - slide 10 of 10'
  -> strip 6 s later, with no further input: identical

NoticeStrip paints a 70%-opaque black band; NoticeStrip::heightFor(1080) = 72 px.

**Impact:** Fires on the FIRST arrow key and never stops — the highest-frequency defect below the top two. From that moment the audience sees a permanent translucent black band across the bottom 72 px (6.7%) of the projector reading 'Slide 2', 'Slide 3', and so on, obscuring the bottom of every slide. A one-off message such as 'End of deck - slide 10 of 10' or 'Deck has 10 slides' from a mistyped jump stays on the wall indefinitely if Karl then speaks for ten minutes without touching the keyboard, including through Q&A. It discloses nothing — the Notice vocabulary is genuinely closed and that was verified — but it is audience-facing text the design explicitly classified as Transient and then never implemented.

**Fix:** Honour the class in AppShell. The tick_ timer already running at 250 ms can drive it: in applyResult(), after setting lastNotice_, set noticeExpiryMs_ = (r.notice.cls == NoticeClass::Transient && !lastNotice_.isEmpty()) ? clock_.elapsed() + 4000 : -1; and in the tick lambda, when noticeExpiryMs_ > 0 and clock_.elapsed() >= it, clear lastNotice_, reset noticeExpiryMs_ to -1 and refresh(). Sticky notices (DeckEmpty, Paused) keep -1 and stay up, which is the documented intent.

### 7. [SEV-3] Ctrl+Shift+F (ToggleFullScreen) and Ctrl+Shift+R (ReRenderDeck) are consumed by the key translator and then silently dropped by AppShell

*Sources: presenter-flow (sole finder); dead default: break confirmed in src/ui/app_shell.cpp:160-161 and the enum values in src/present/key_translator.hpp:27-29 during consolidation*

**Repro:** Confirmed by me in source. src/present/key_translator.cpp:58-72 produces UiRequest::ToggleFullScreen for Ctrl+Shift+F and UiRequest::ReRenderDeck for Ctrl+Shift+R and sets consumed=true on both. src/ui/app_shell.cpp:140-161 handles RequestHolding, RequestQuitConfirm, CancelQuit, MoveSlideWindowToNextScreen and ConfirmQuit — ToggleFullScreen and ReRenderDeck fall into `default: break;`.

  ./harness/b/presenter_harness "$PWD/decks" s21
  -> Ctrl+Shift+F: fullScreen 1 -> 1   [FAIL] Ctrl+Shift+F actually does something
  -> Ctrl+Shift+R: raster restored = 0 [FAIL] Ctrl+Shift+R actually re-renders

**Impact:** Two documented recovery chords do nothing, and because the key IS consumed there is no fallback either — the press is swallowed with no notice, which reads as a frozen app under stage pressure. Ctrl+Shift+F is the only documented way out of fullscreen (Esc is deliberately disabled and close is refused), so if Karl needs to leave fullscreen to reach another app mid-talk he is stuck with OS-level window management on a window that refuses to close. Ctrl+Shift+R is the only documented recovery if a slide renders wrong. Note Ctrl+Shift+D, the projector escape hatch, IS wired and was verified working — it is only these two that are dead.

**Fix:** Wire both cases in AppShell's uiRequestSink: ToggleFullScreen -> window_->isFullScreen() ? window_->showNormal() : window_->showFullScreen(); ReRenderDeck -> tear down the render worker and restart it against deck_ (PreRenderWorker already has the running_ re-entrancy guard for exactly this, and that guard was re-verified this session). If either is not going to ship for 2026-08-10, remove it from the translator so the key is not consumed and the presenter gets normal behaviour instead of silence — that is a smaller change than wiring it and strictly better than the status quo.

### 8. [SEV-3] Typed slide numbers give the presenter zero feedback, and the translator's 'slide number too long' notice has no route to any surface

*Sources: presenter-flow (sole finder); the dropped a.notice confirmed in src/ui/presentation_window.cpp:54-60 during consolidation*

**Repro:** Confirmed by me in source. src/ui/presentation_window.cpp:54-60 forwards a.command and a.uiRequest to their sinks but never reads a.notice — so the Notice{SlideNumberTooLong} raised at src/present/key_translator.cpp:96 is constructed and discarded. KeyCommandTranslator::pendingDigits() (key_translator.hpp:53) exists and is never called by anything.

  ./harness/b/presenter_harness "$PWD/decks" s22
  -> after typing '1': strip='' translatorBuffer='1'
  -> after 8 digits: strip='' buffer='199999'
  [FAIL] the half-typed slide number is echoed somewhere the presenter can see
  [FAIL] the 'slide number too long' notice reaches the presenter

**Impact:** On a 10-slide deck Karl types '1','0',Enter with nothing on screen confirming what he typed. A mistyped digit produces a silent rejection — 'Deck has 10 slides' only appears after Enter — and he cannot distinguish a swallowed keypress from a dead keyboard, which is precisely the situation the audited keyboard fallback exists to cover. Lower frequency than the items above because arrow keys are the primary navigation on a 10-slide deck and typed jumps are mainly a Q&A tool; the 3 s staleness rule that prevents a stale digit from prefixing a new number was separately verified working, so the dangerous half of this is already handled.

**Fix:** In PresentationWindow::keyPressEvent add `if (a.notice && noticeSink_) noticeSink_(*a.notice);` and route it through AppShell to the notice strip. Separately, echo translator_.pendingDigits() to the strip on every digit, e.g. 'Go to slide 1_', and clear it on Enter or a mode change. Both changes interact with item 6 — the digit echo should be Sticky until Enter, not Transient.

### 9. [SEV-3] The privacy-blackout screen shows the audience an operator instruction, and the instruction is factually wrong ('any key to resume')

*Sources: presenter-flow (SEV-3) and regression-guard (SEV-4) independently; the Operator-role-on-the-audience-surface call confirmed in src/ui/app_shell.cpp:230-232 during consolidation*

**Repro:** Confirmed by me in source. src/present/notice.cpp:35-36 returns 'Presentation paused - press Esc again to exit, any key to resume' for NoticeId::HoldingHint. src/ui/app_shell.cpp:230-232 paints it on the fullscreen AUDIENCE-facing SlideSurface while asking for NoticeRole::Operator — and noticeForRole ignores the role for this id, so the operator string reaches the audience either way. Every other notice goes through applyResult() with NoticeRole::Audience.

'any key' is false: src/present/presentation_controller.cpp:117-120 leaves Holding only on an ACCEPTED navigation.
  ./harness/b/presenter_harness "$PWD/decks" s25
  -> after 'P': mode=Holding      -> after 'A': mode=Holding
  -> after 'Enter': mode=Holding  -> after 'Space': mode=Presenting

**Impact:** During the privacy blackout the projector shows the room a line of presenter instructions telling them how to un-blank Karl's deck, and the instruction is wrong. If he taps P or Enter expecting the deck back, nothing happens and he is left pressing keys at a black screen in front of the audience. 'press Esc again to exit' is also misleading — Esc goes to a quit prompt, which is then itself displayed to the audience. It leaks no deck content, but it is on the audience screen at the single most sensitive moment the product has, and it fires on exactly the same flow as item 1. Fix them together.

**Fix:** Split the string by role and make the audience version content-free: Audience -> 'Presentation paused' (or empty), Operator -> 'Paused - Esc again to exit, or an arrow key to resume'. Then correct the caller in app_shell.cpp:231 to NoticeRole::Audience to match applyResult(). This is the same role-policy gap already logged as BUG-19 (the audience/operator split is implemented for only 2 of 11 notice ids), but HoldingHint is now a live caller of it, so it is no longer theoretical.

### 10. [SEV-3] Keyboard focus is not restored to the presentation window after the modal error dialog closes, and the dialog is unparented

*Sources: presenter-flow (sole finder, with an explicit platform caveat)*

**Repro:** src/ui/app_shell.cpp:107-131 — both failure paths pop an app-modal QMessageBox parented to nullptr and return without touching window_. The success path (lines 204-207) does raise(), activateWindow() and setFocus(); the failure paths do not.

  ./harness/b/presenter_harness "$PWD/decks" s29
  -> focusWidget before: pptv::PresentationWindow
  -> [PASS] dialog appeared and was dismissed
  -> focusWidget after: (none)   activeWindow: (none)
  -> [FAIL] keyboard focus returned to the presentation window after the dialog

HONEST CAVEAT recorded by the tester: measured under the offscreen QPA plugin, whose activation model is weaker than a real window manager, so focus loss may be less absolute on real hardware. The code observation — nothing re-focuses the window after the dialog — is not platform-dependent.

**Impact:** If Karl picks the wrong file mid-talk and dismisses the error, the arrow keys may no longer reach the deck because the parentless modal dialog took activation and nothing gave it back. The recovery — click the projector window — is not obvious under pressure and on a fullscreen window on a second display it may not be clickable from the laptop at all. The unparented app-modal dialog is a stage risk in its own right: it freezes the deck and can land on a different screen from the one the presenter is watching. Reachability is lower than it looks in v0.1 because there is no in-app way to open a second deck (openDeck has one caller), so a mid-talk bad open requires relaunching — but the same dialog is on the launch path shared with item 5.

**Fix:** Parent the QMessageBox to window_ when a presentation is live, so it becomes a sheet on the right screen rather than a stray top-level, and after it returns call window_->raise(); window_->activateWindow(); window_->setFocus() on the failure paths exactly as the success path does. Better still, replace the modal box mid-talk with a NoticeStrip message so a bad file never blocks a live presentation at all.

### 11. [SEV-3] 8 of the 178 ctest entries execute ZERO test cases — and the 4 real cases they should run cover the blackout, the two-step quit, BUG-16 and BUG-11/17

*Sources: automated-suite (sole finder)*

**Repro:** Four doctest TEST_CASE names contain a ';'. CMake treats ';' as a list separator, so doctest's discovery splits each into two ctest entries whose --test-case= filter matches nothing; doctest exits 0 on zero matches, so ctest prints 'Passed'.

  cd build && ctest -V -R '^A: one past the end is rejected$' 2>&1 | grep -E 'test cases:|Passed'
  -> [doctest] test cases: 0 | 0 passed | 0 failed | 165 skipped
  -> 1/1 Test #102: A: one past the end is rejected ... Passed 0.02 sec

The arithmetic: 165 (pptv_tests) + 9 (pptv_ui_tests) = 174 real cases, but `ctest -N | grep -cE '^  Test +#'` = 178.

The four cases that never execute:
  A: one past the end is rejected; the last slide is reachable
  D: Esc goes to the holding screen; a command returns to presenting
  D: a second Esc asks to quit; cancel returns to holding
  UAT2 BUG-11/17: 'resume presentation' un-pauses; a bare word does not
All four PASS when invoked directly with --test-case=, so nothing is broken today. Hits both scripts/run-tests.sh:28 and .github/workflows/ci.yml:44.

**Impact:** Zero live impact — all four cases pass when actually run. The damage is to the safety net over the final six days, and it lands exactly where this session's work is about to land: the four dead cases protect BUG-16 range rejection and last-slide reachability (slide 10 is the end of Karl's talk), Esc -> privacy blackout -> return to presenting (item 1's fix touches precisely this), the two-step quit and its cancel path (item 3's fix touches precisely this), and the BUG-11/17 resume wording. 21 assertions never run. Any regression introduced by the fixes above would ship with a fully green 178/178 gate locally and in CI. Secondary hazard: '178 tests expected green' is not a safe invariant — adding a semicolon to a test name RAISES the count while REMOVING coverage, so the number cannot detect this class of loss.

**Fix:** Remove the semicolons — they are purely cosmetic. tests/test_presentation_controller.cpp lines 58, 199, 210 and tests/test_recognizer_controller.cpp:170; replace ';' with ' and ' or an em dash (commas are already escaped correctly by doctest.cmake — only ';' breaks). Do this BEFORE fixing items 1 and 3 so those fixes are actually gated. Then assert the invariant instead of the count: add a CI step comparing `ctest -N | grep -cE '^  Test +#'` against the summed --list-test-cases counts of both binaries and fail on mismatch — that also catches any future semicolon.

### 12. [SEV-3] The 1 GB decompression cap is charged per zip entry but slide/rels/layout parts are re-read per slide, and the resulting long load cannot be cancelled

*Sources: hostile-input (sole finder, SEV-2 as reported)*

**Repro:** src/loader/deck_loader.cpp — the cap loop at lines 561-590 walks the central directory and charges st.size once per ENTRY, but nothing charges per READ. Three call sites re-read unboundedly inside the per-slide loop: the slide part at line 711, the slide rels at 724, and the slideLayout at 735. The audit C5 fix at lines 762-776 solved exactly this for media parts via mediaCache + mediaBytes; the same treatment was never extended to the XML parts.

  ./build/render_preview .../a22_combined_amp.pptx /tmp/out
  -> exit=0, wall=66.7s, peak RSS 425 MB. Archive on disk: 263,690 bytes.
Matched control isolating loader cost from render cost (300 slides, tiny layout): wall=5.9s -> ~60.8 s is pure DeckLoader::load re-read.
The archive DECLARES 254 MB uncompressed (well under the 1 GB cap) but the loader actually decompresses 300 x (127+127) MB ~= 76 GB.

**Impact:** Severity re-judged DOWN from the reporter's SEV-2. The 300x multiplier requires 300 slides; on Karl's 10-slide deck the same mechanism gives at most 10x, and it needs a deliberately crafted archive that his own deck is not. So this is not a live risk for 2026-08-10 — it is a real zip-bomb-cap bypass (a factor of 300 without ever tripping TM-014/TM-017) that should be fixed after the talk. The one part worth noting NOW is the cancellability half, which is architecturally the same defect as item 3: DeckLoader::load takes no cancellation token and checks no flag in its body, and DeckLoadWorker::start only tests cancelled_ before the parse and after it returns (deck_load_worker.cpp:56, 90). A long load is therefore un-abortable, and teardownWorkers then reaches QThread::terminate() on a thread suspended inside libzip inflate or pugixml allocation — potentially killing a thread holding the malloc lock. Peak RSS stayed at 425 MB throughout, so this never OOMs; it is wall-clock and cancellability only.

**Fix:** Hoist the caching one level up so it is structural rather than per-call-site: give readPart a QHash<QString, QByteArray> cache keyed by part name and a running bytesRead budget checked on every cache MISS, then route every part read through it (slide, rels, layout, theme, master, media alike). That removes the amplification and makes the cumulative cap mean what its comment claims. Cheaper interim: cache parsed layouts keyed by layoutPart and slideXml keyed by slidePart, which kills the multiplier for the two dominant vectors. SEPARATELY AND SOONER: give DeckLoader::load a `const std::atomic<bool>& cancelled` parameter checked at the top of the per-slide loop — that is the load-side twin of item 3's fix and removes the last reason teardownWorkers ever needs terminate().

### 13. [SEV-3] libpng writes deck-derived bytes straight to the app's stderr, bypassing Qt's logging categories (TM-012/TM-013)

*Sources: hostile-input (sole finder)*

**Repro:** Craft a media part whose PNG chunk-type field carries recognisable content and open it with the REAL app binary:
  QT_QPA_PLATFORM=offscreen ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice .../a26_content_to_stderr.pptx
  -> stderr: libpng error: ACQU: CRC error
The four bytes 'ACQU' are read verbatim out of the media part inside the deck. Second instance with random media bytes: 'libpng error: [A9]Q[FC][9E]: bad header (invalid type)'.

Two distinct channels matter here: Qt's own decoder messages are tagged 'qt.gui.imageio:' and ARE suppressible via QLoggingCategory, but the bare 'libpng error:' line comes from libpng's default error handler writing to stderr directly and is filtered by nothing.

**Impact:** Project Bible section 8 / TM-012 / TM-013 require that deck CONTENT never reach a log or any persisted artefact, and this is a live channel that does exactly that. The volume is small (a 4-byte chunk-type field, and only for images that fail to decode) and it never reaches the projector — but for a .app launched from Finder, stderr is captured by the macOS unified logging system and persisted to disk outside the application's control, which is precisely the outcome the constraint exists to prevent. Critically this applies to Karl's real Confidential deck and not only to a hostile one: any image in it that trips a libpng warning path emits bytes from that image into the system log. This is the ONLY content-leak channel any of the five testers found — everything else (Notice vocabulary, LoadError::message, dialogs, the deck path) was independently confirmed sealed.

**Fix:** src/render/slide_renderer.cpp:41-59, decodeGuarded(). The allow-list and the 40 Mpx / 128 MiB bounds are doing their job — the images are correctly rejected and a placeholder drawn — but the codec is still invoked on deck bytes and is allowed to narrate its failures to stderr. Install a qInstallMessageHandler in main.cpp that drops or redacts anything in the qt.gui.imageio category, and suppress libpng's own handler — the practical route is to sanity-check the PNG/JPEG header and chunk structure before handing the buffer to QImageReader, so structurally broken images never reach libpng at all. decodeGuarded already parses enough to check format() and size(), so this is a small extension with the side benefit of rejecting malformed images faster.

### 14. [SEV-3] C1 residual: QPointer's check-then-use is not atomic across threads — showSlide() was caught calling invokeMethod on a partially-destroyed worker

*Sources: regression-guard (sole finder, as a residual of the verified-fixed C1)*

**Repro:** The audit's claim at src/ui/app_shell.hpp:56-61 is that 'QPointer self-nulls, so every if (worker_) guard in this file becomes truthful'. That is false across threads: the worker is destroyed ON THE WORKER THREAD (QThread::finished -> deleteLater is a direct connection because the receiver lives there), while `if (renderWorker_)` and the invokeMethod that follows it run on the GUI thread (src/ui/app_shell.cpp:281-285). QPointer's weak ref is only cleared late inside ~QObject.

120 separate processes, ONE openDeck each — the exact product path — with arrow keys spammed across the instant pre-render completes:
  for i in $(seq 1 120); do QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe hammer <scratchpad>/fix/good10.pptx 1; done | grep -c 'No such method'
  -> 7 / 120 printed: QMetaObject::invokeMethod: No such method QObject::setCurrentIndex(int)

That message can only come from reading the metaObject of an object whose derived destructor has already run (vptr downgraded to QObject) but whose ~QObject has not yet cleared the QPointer. Reproduced in Release, ASan+UBSan and TSan builds. TSan reports ZERO races on the same run — correct, because the weak ref is atomic; this is a lifetime/ordering bug, not a data race, which is why the clean TSan result did not catch it.

**Impact:** The observed consequence on stage is benign to moderate: the re-steer is silently lost, so the renderer does not learn which slide the presenter jumped to and keeps rendering in its old order, leaving him on 'Rendering slide N...' longer than necessary. But it is an unsynchronised cross-thread dereference of an object mid-destruction — undefined behaviour on the live path — and QCoreApplication::postEvent is being called against a receiver whose thread data is being torn down. It fired in ~6% of loads under deliberate key pressure; a real presenter presses far fewer keys in that window, so the practical rate is much lower. The reason to fix it is that C1 was closed on a guarantee that does not exist, so the next change in this area will assume a safety property that is not there. Note presenter-flow hammered the same race 40 rounds and could NOT crash it — the two results are consistent: it is silent, not fatal.

**Fix:** Stop using a QPointer dereference for a cross-thread call; use a signal, which Qt emits under its own connection lock and which handles receiver destruction correctly. Add `signals: void currentIndexChanged(int index0Based);` to AppShell, connect it to PreRenderWorker::setCurrentIndex alongside the other connects in openDeck(), and replace the invokeMethod block in showSlide() with `emit currentIndexChanged(index1Based - 1);`. That also removes the invokeMethod-by-name, which is why the failure was silent in the first place — a string lookup that misses only warns.

### 15. [SEV-4] .github/workflows/release.yml is an invalid workflow file — every push to every branch logs a permanent red run next to the green one that matters

*Sources: cross-platform (sole finder)*

**Repro:** gh run list --workflow='.github/workflows/release.yml' --limit 5 -> 5/5 completed failure, 0s each, on main AND feature branches.
gh run view 30959310452 -> 'This run likely failed because of a workflow file issue.'
Root cause in the file itself: `- uses: # TODO: Add setup action for your language` — a `uses:` key with an empty value is a schema error, so GitHub cannot parse the workflow and records a 0s failure against the push. `on: push: tags: ['v*']` does NOT prevent this; invalid workflows fail on the triggering push regardless of the trigger filter. Seven consecutive pushes in 12 hours produced the pair.
The real gate is green: run 30959310972, job `test` SUCCESS 1m27s, 178/178 on ubuntu-24.04; job `sast` SUCCESS.

**Impact:** github.com/kraulerson/powerpoint-voice always shows a failing check on main. Six days out, that is the classic way a real failure gets ignored — the job that matters sits next to a red X everyone has learned to scroll past, and items 1 through 5 are about to generate new pushes. It also means no signed or checksummed build can ever be produced, so if anything goes wrong on 2026-08-10 there is no released artefact to fall back to, only Karl's build tree.

**Fix:** Park the pipeline; do not try to wire up code signing this week. Change the trigger to `on: workflow_dispatch:` with the tag trigger commented out for later, and delete the invalid step body (the `- uses: # TODO ...` block and the empty `run: # TODO ...` steps) or replace them with `run: echo 'TODO'`. The SBOM step already exit 1s deliberately, so workflow_dispatch alone still cannot produce a bad release.

### 16. [SEV-4] Three CI gates report green while verifying essentially nothing: semgrep on C++, two missing governance scripts, and a skipped clang-tidy step

*Sources: automated-suite (semgrep positive control) and cross-platform (governance scripts, dead clang-tidy step, macOS sysroot) — merged as one gate-credibility item*

**Repro:** (a) SEMGREP IS VACUOUS FOR C++. `semgrep --config p/owasp-top-ten src/` -> Findings: 0, Rules run: 5, Targets scanned: 37; p/security-audit -> Rules run: 2. Positive control: a C++ file containing a strcpy overflow, sprintf+system() injection, execl with user input, a double free and a format-string bug produced 0 findings from BOTH packs, while an inline rule with languages:[cpp,c] flagged them instantly.
(b) TWO GOVERNANCE STEPS CALL SCRIPTS THAT DO NOT EXIST. `ls scripts/check-changelog.sh scripts/check-session-state.sh` -> No such file or directory, both. ci.yml runs them as `bash scripts/check-changelog.sh 2>/dev/null || true` — both appear as green steps 16 and 17 with zero output; 2>/dev/null eats the error and || true eats the exit code. The same file's own Phase-gate comment explicitly forbids this pattern.
(c) CLANG-TIDY NEVER RUNS. No .clang-tidy exists, so the `if: hashFiles('.clang-tidy') != ''` step is silently skipped (confirmed absent from the 19 steps of job 92159466139). Enabling it naively would emit 837 diagnostics and turn main red — 623 cert-err33-c all in tests/, 39 bugprone-macro-parentheses all from doctest.h, which an unanchored HeaderFilterRegex of 'src/.*' also matches via build/_deps/doctest-src/.

**Impact:** No stage impact whatsoever — this is about how much the green tick on main is worth in the final six days. It is worth less than it looks: the C++ security gate cannot see C++ (which matters because src/loader/deck_loader.cpp parses untrusted .pptx and item 4 in this list is a stack overflow in exactly that path that no static gate caught), two named checks verify nothing at all, and the lint gate is off. Worth stating plainly given items 1-5 will all be landed under time pressure against this gate. The honest counterweight, verified this session: what the clang-tidy gate WOULD find in the new presenter code is essentially nothing — zero bugprone, zero cert, zero clang-analyzer and zero concurrency findings across all 11 files in src/present/ and src/ui/, with the only substantive hit being the already-known BUG-23 const std::move.

**Fix:** All three are post-talk work; landing them this week is the wrong call. (a) Stop quoting semgrep as the C++ security gate — either drop the two packs or record honestly that they apply 5 and 2 rules. The high-value replacement given the untrusted-input surface is a libFuzzer or AFL++ harness over DeckLoader::load() seeded with tests/fixtures/bad_*.pptx under ASan+UBSan, which would have found item 4 immediately. (b) Delete the two governance steps or make them fail loudly (`if [ ! -f ... ]; then echo '::error::...'; exit 1; fi`) — deleting is the honest six-days-out choice. (c) When clang-tidy does land, scope it to product code with an ANCHORED HeaderFilterRegex ('(^|/)src/(core|loader|render|command|model|present|ui)/') so doctest-src cannot match, narrow the ci.yml step to `git ls-files 'src/*.cpp'`, and configure with -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)" — without it Homebrew clang-tidy cannot parse the project's own compile database on macOS, reports 'Error while processing' on all 11 files, and still prints plausible-looking findings from the broken AST that differ from the correct ones.

### 17. [SEV-4] LATENT until voice ships: undoJump has no pause gate, and the blackout un-hold is source-blind so an audience 'next slide' would reveal the deck

*Sources: regression-guard (sole finder); the single hard-coded CommandSource call site confirmed by grep during consolidation*

**Repro:** Confirmed by me: `grep -rn 'CommandSource::' src/` returns exactly TWO hits — the gate itself at presentation_controller.cpp:46 and the single product call site at app_shell.cpp:138, hard-coded to CommandSource::Keyboard. RecognizerController is never instantiated by AppShell, and undoJump has no product caller at all (only tests).

(a) undoJump() takes no CommandSource and no `paused` flag, so the pause gate structurally cannot see it:
  voice NextSlide while paused: outcome=Suppressed slide=40
  undoJump (no source/paused parameter exists): outcome=Moved slide=1
(b) The un-hold at presentation_controller.cpp:117-120 tests only isNavigation and the outcome, never the source:
  voice next slide, NOT paused -> outcome=Moved mode=Presenting slide=21  *** BLACKOUT DISMISSED ***
  voice go-to-slide 7, NOT paused -> outcome=Moved mode=Presenting slide=7 *** BLACKOUT DISMISSED ***
All the paths HIGH-2 explicitly named ARE correctly held — a rejected command, keyboard pause, voice-continue-while-paused and voice-nav-while-paused all leave the blackout up.

**Impact:** Nothing on 2026-08-10: the shipped app is keyboard-only and nothing calls undoJump. Recorded because the blackout is raised precisely when the room is listening and, under F5, the mic would be live and unpaused — someone walks in, Karl hits Esc, and a single audience utterance of 'next slide' would both reveal the Confidential deck and advance it. That is the TM-002/019 scenario verbatim, and the HIGH-2 fix does not cover it because the un-hold never looks at the source. Worth closing while the funnel is still small rather than after F5 adds callers.

**Fix:** (a) Give undoJump the same signature shape as dispatch so no call site can omit the gate: DispatchResult undoJump(CommandSource src, bool paused), running the same preamble — Suppressed under ConfirmQuit (already there), Rejected on an empty deck, Suppressed when paused && src == CommandSource::Voice. (b) Make the un-hold source-aware: add `src == CommandSource::Keyboard` to the condition at presentation_controller.cpp:117. The blackout is a deliberate act by the presenter, so only the presenter's own audited input device should end it; voice navigation while Holding should be Suppressed and leave the blackout up. Assert both in tests. Do this in the same change as item 1 — they are both about who is allowed to end the blackout.

### 18. [SEV-4] LATENT: quitConfirmed_ is never reset by setDeck(), so a deck opened after a confirmed quit would be closed by the first key press

*Sources: presenter-flow (sole finder); missing reset confirmed in src/present/presentation_controller.cpp:14-20 during consolidation*

**Repro:** Confirmed by me in source. src/present/presentation_controller.cpp:14-20 — setDeck() resets slideCount_, current_, haveJumpUndo_, jumpUndoTarget_ and mode_, but NOT quitConfirmed_. src/ui/app_shell.cpp:296-298 — applyResult() calls window_->close() on every dispatch while quitConfirmed() is true, and presentation_window.cpp:75-78 stops refusing closes.

  ./harness/b/presenter_harness "$PWD/decks" s16
  -> [PASS] quit confirmed  -> [PASS] deck B loaded
  -> after reopen: quitConfirmed=1 mode=Presenting windowVisible=1
  -> after ONE arrow key: visible=0

**Impact:** Not reachable in v0.1 — I confirmed openDeck has exactly one caller (main.cpp:27) and a confirmed quit closes the last window, so QApplication exits. It becomes a real bug the instant a file-open action, a recent-files list or drag-and-drop lands on the StartView — which is exactly what item 5's recommended fix adds. If item 5 is fixed without this one, the next session would open, show slide 1, and then close itself on the first arrow key.

**Fix:** One line: add `quitConfirmed_ = false;` to PresentationController::setDeck(), plus a controller unit test asserting setDeck() clears it. Land it TOGETHER with item 5's StartView open button, never after.

### 19. [SEV-4] One genuinely flaky test (~3-4% of runs) on the deck-load cancel path — a racy QSignalSpy assertion, not a product defect

*Sources: automated-suite (sole finder, with a 600-run root cause and a 400/400 corrected-assertion control)*

**Repro:**   cd build
  f=0; for i in $(seq 1 450); do ./tests/pptv_tests --test-case='P: a result arriving after cancel is discarded\, never delivered late' >/dev/null 2>&1 || f=$((f+1)); done; echo "$f failures / 450"
  -> 19 failures / 450 (4.22%); 20/600 combined. Or: ctest --repeat until-fail:40 -R '^(O|P):'
Failure: tests/test_deck_load_worker.cpp:118: REQUIRE( finishedSpy.wait(5000) ) is NOT correct, test time 4.83 s (the full timeout).

PROVEN to be the assertion, not the product, via <scratchpad>/qa-baseline/repro23b.cpp: mode 0 (assertion as written) = 14 FAIL / 400, and EVERY failure printed finishedHits=1 loadedHits=0 loadFnEntered=1 with 0 thread hangs; mode 1 (tolerates an already-delivered signal, same product code) = 0 FAIL / 400, max wait 15 ms.

**Impact:** No stage risk — the product is correct and was verified rather than assumed: DeckLoadWorker::start() emits finished() on all three exit paths, in all 14 instrumented failures it had emitted finished() exactly once and loaded() zero times (the late result really is discarded, which is the behaviour the test is named for), and the worker thread exited cleanly in 400/400 trials. The cost is to the pre-flight gate in the final six days: roughly 1 in 25-30 full ctest runs goes red spuriously at a 5-second stall each, higher on a slower CI runner. The real danger is behavioural — a gate that cries wolf trains the team to re-run and shrug, which is how a genuine regression in the deck-load cancel path gets waved through on the morning of the talk. Compounds item 11.

**Fix:** QSignalSpy::wait() snapshots the count on entry and returns true only if a NEW emission arrives afterwards, but the worker is expected to emit finished() the instant gate.release() returns — concurrently with the main thread reaching wait(). Replace `REQUIRE(finishedSpy.wait(5000));` at tests/test_deck_load_worker.cpp:118 with a deadline poll: QDeadlineTimer dl(5000); while (finishedSpy.isEmpty() && !dl.hasExpired()) { QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1); } REQUIRE(finishedSpy.count() >= 1); (QTRY_VERIFY does exactly this if QtTest's macros are available). Sweep the other spy.wait() sites in test_deck_load_worker.cpp and test_pre_render_worker.cpp for the same latent pattern, and consider replacing the QThread::msleep(50) on line 115 with a semaphore signalled on load-fn entry.

### 20. [SEV-4] src/ui/app_shell.cpp — 301 lines wiring both workers to the GUI thread — has ZERO test coverage, and TSan cannot certify that boundary either

*Sources: automated-suite (coverage gap and the TSan control experiment); corroborated implicitly by presenter-flow, regression-guard and cross-platform, all three of whom had to build custom harnesses to test this file*

**Repro:**   grep -rln 'app_shell\|AppShell' tests/   -> no output
  grep -rln 'PresentationWindow' tests/    -> only tests/ui/test_widgets.cpp
  wc -l src/ui/app_shell.cpp                -> 301
The 9 widget tests cover slide_surface, notice_strip and key routing only.

TSan's assurance across the Qt signal boundary is also weak, and this was proven rather than asserted: a pure-Qt control containing ZERO product code (<scratchpad>/qa-baseline/tsan_control.cpp) reproduces the same 'race' class the product build reports (2 racing runs / 60 at qarraydatapointer.h:36), because QSignalSpy's happens-before edge lives inside uninstrumented QtCore. The same product path with a TSan-VISIBLE std::mutex + join edge produces 0 warnings across all 8 error kinds.

**Impact:** Recorded last, but it is the finding that explains the whole session: EVERY confirmed SEV-1 and SEV-2 defect in this report except the loader recursion lives in app_shell.cpp or the geometry it drives — the blackout leak, the teardown abort, the double letterbox, the dropped UiRequests, the null start_. The two workers are each well tested in isolation (groups P and O, 19 cases), but the code that connects them to the window, marshals QImage onto the GUI thread and enforces 'QPixmap must never be touched off the GUI thread' is exercised only by launching the real app by hand. Three testers each had to build a bespoke harness to reach it. A TM-018 violation or a cross-thread QPixmap touch introduced there between now and the talk would be caught by neither the 178-test gate nor the sanitizer builds — only by someone noticing a frozen or black projector during a rehearsal.

**Fix:** Add a small headless integration test that constructs AppShell under QApplication with QT_QPA_PLATFORM=offscreen, feeds it tests/fixtures/good_text.pptx, and asserts the observable contract: that loaded/rendered signals arrive on the GUI thread (QThread::currentThread() == qApp->thread() inside the slot), that a slide raster reaches the surface, that over-cap slides become placeholders without entering the renderer, and that cancelling a load mid-flight leaves the shell sane. Three or four cases would cover the wiring these 301 lines currently carry — and the very first one should be item 1's regression test. Do NOT add TSan suppressions for the 10-12 qvariant_cast warnings or chase them as bugs; they are QSignalSpy artefacts. Record instead that TSan gives only weak assurance through Qt, so the worker-to-GUI contract must be asserted by explicit thread-affinity checks rather than inferred from a clean run.

### 21. [SEV-4] Ubuntu CI headlessness rests on one hardcoded qputenv line, which the Build step also depends on and which overrides any CI-set platform

*Sources: cross-platform (sole finder; also the tester's headline negative result — Q2 works, no YAML fix is required for CI to be green today)*

**Repro:**   grep -n QT_QPA_PLATFORM .github/workflows/ci.yml -> no match
The only thing making it work is tests/ui/ui_test_main.cpp:9 and tests/test_main.cpp:9: qputenv("QT_QPA_PLATFORM", "offscreen") before constructing QApplication/QGuiApplication.
The BUILD depends on it too: doctest_discover_tests registers a POST_BUILD command that RUNS the binary (build/_deps/doctest-src/scripts/cmake/doctest.cmake:136), and ui_test_main.cpp constructs QApplication before ctx.run(), so --list-test-cases at build time constructs a QApplication.
Proof the hardcode beats the environment: QT_QPA_PLATFORM=xcb and QT_QPA_PLATFORM=totally_bogus both run fine against the binaries; either should abort if the env var were honoured.

**Impact:** Nothing today — main is green, 178/178 including the nine 'U:' widget tests, and the offscreen plugin genuinely ships via qt6-base-dev's hard dependencies (libqt6gui6t64, qt6-qpa-plugins, installed under --no-install-recommends). The risk is a confusing red six days out: whoever tidies up 'why is a test hardcoding an environment variable' breaks `cmake --build build` on Ubuntu, not just ctest, with a message ('Could not load the Qt platform plugin "xcb"') that looks nothing like a test failure. Second-order and more interesting: because qputenv wins, the widget tests can never run under xcb/xvfb, so offscreen is the only surface they ever see — and offscreen has no window manager (the app itself logs 'This plugin does not support raise()'). That is why AppShell's showFullScreen/setScreen/raise/activateWindow path and moveWindowToNextScreen are covered by zero automated tests on any platform, and it is directly connected to item 2 going undetected.

**Fix:** Safe today, costs nothing: add `env: QT_QPA_PLATFORM: offscreen` to BOTH the Build and Test steps in ci.yml (Build runs the binaries too). Do NOT change the qputenv line before 2026-08-10 — it is the line CI currently depends on; after the talk, make it conditional (`if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv(...)`) so CI can override. The fullscreen and multi-screen path stays a hands-on UAT item that no headless CI can cover — which, given item 2, means a rehearsal against the actual projector is mandatory regardless of what is fixed.

### 22. [SEV-4] The render path has no decoded-image cache, so one media part is decoded once per <p:pic> that references it

*Sources: hostile-input (sole finder, who flagged the BUG-21 overlap themselves)*

**Repro:** src/render/slide_renderer.cpp:240 calls decodeGuarded(e.image.imageData) inside the per-element z-order loop with no memoisation, so N references to one part cost N full decodes.
  ./build/render_preview .../a12_pic_amplify_2000.pptx /tmp/out
  -> exit=0, wall=8.6s for ONE slide, peak RSS 47 MB (3,557-byte deck; 2000 <p:pic> all referencing a single 4000x4000 PNG)
Shape count 2000 is deliberate: RenderCaps::maxShapesPerSlide is 2000 and exceedsCaps() tests `>`, so exactly 2000 passes the PREVENT gate and enters the renderer.
Contrast the LOAD side, which the tester was asked to verify and which HOLDS: the same 100 MB part referenced by 2000 <p:pic> loads in 0.3 s at 149 MB peak RSS (pre-C5 this would have been ~200 GB resident).

**Impact:** OVERLAP DECLARED: this is a specific instance of the general class described by known-deferred BUG-21 (the TM-018 caps count shapes and text runs, not actual work). It is listed rather than dropped because the mechanism is narrow, distinct and independently fixable, and because the fix is a near-copy of one already in the tree — but triage should decide whether to fold it into BUG-21 rather than track it twice. No live risk for a 10-slide text deck. Because rendering is off-thread (the ISOLATE leg of TM-018), even at 8.6 s the UI thread does not block and the presenter can still drive the deck; the affected slide simply is not ready when he arrives and he gets a placeholder or a stale raster.

**Fix:** Mirror the load-side C5 fix on the render side: add a QHash<QString, QImage> keyed by e.image.mediaPart, local to SlideRenderer::render, and reuse the decoded QImage across elements within a single slide render. QImage is implicitly shared, so the cache costs one decode and one buffer regardless of reference count, and the change is contained entirely within the Image case of that switch.

### 23. [SEV-4] 'go to slide -5' jumps to slide 5 — a leading minus is silently consumed as a token separator

*Sources: automated-suite (sole finder, self-flagged as negligible)*

**Repro:**   ./build/command_probe 'go to slide -5'   ->  GoToSlide(5)
Cause: src/command/number_parser.cpp:parseSlideNumber splits on [\s\-]+ so hyphenated number words ('twenty-five') work; a leading '-' therefore becomes a separator rather than a sign, and '-5' tokenises to ['5'].
Neighbouring inputs are all correct: 'go to slide 2147483648', 'go to slide 999999999999999999999', 'go to slide banana' and bare 'go to slide' all yield no command.

**Impact:** Negligible for the live talk, and the reporter was explicit about not inflating it. The voice path cannot produce this string (a recognizer emits number words), the keyboard path does not route through this parser at all (key_translator handles typed digits separately with a capped buffer), and voice is not wired to the controller anyway. Reaching it requires typing a literal minus into command_probe, a dev tool. Listed only because it is a real divergence between what the grammar accepts and what it appears to accept: a negative slide number is not rejected, it is silently reinterpreted as its absolute value, and presentation_controller then range-checks 5 as perfectly valid in a 10-slide deck — a silent wrong jump where the design intends a loud rejection everywhere else.

**Fix:** In parseSlideNumber, reject a sign rather than swallowing it: before the split, return std::nullopt if the trimmed input begins with '-' or '+', or narrow the split so '-' is only a separator between two word characters. Guard it with a test alongside the existing 'audit F1: overflow / token flood rejects, never negative' case, which covers overflow but not an explicit sign. Do not let this compete with anything above it for the remaining six days.

### 24. [SEV-4] The macOS .app bundle is non-relocatable and ad-hoc signed — it runs only on the machine that built it

*Sources: cross-platform (sole finder)*

**Repro:**   otool -L build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice
  -> absolute Homebrew paths (/opt/homebrew/opt/libzip/..., /opt/homebrew/opt/qtbase/lib/QtWidgets.framework/...), no @rpath, no bundled frameworks
  find build/powerpoint_voice.app -type f  -> exactly two files: Contents/Info.plist and Contents/MacOS/powerpoint_voice
  codesign -dv ...  -> Signature=adhoc, TeamIdentifier=not set, Sealed Resources=none
  spctl -a -t exec -vv ...  -> 'code has no resources but signature indicates they must be present'
CFBundleIdentifier is still the template value com.yourcompany.4748f7c7.powerpoint_voice.

**Impact:** Zero impact if Karl presents from the build tree on his own Mac, which is what the documented recipe says and what was verified end to end. It matters only for the contingency plan: if the laptop dies and someone copies the .app to a colleague's machine it will not launch (missing Homebrew Qt) and Gatekeeper would block it anyway. Recorded so nobody plans that as the fallback.

**Fix:** No code change before the talk. Make the fallback plan explicit and correct instead: the backup is 'clone the repo and rebuild with the documented recipe on a Mac with brew install qt libzip pugixml' — measured at about 90 seconds for a fresh clone plus build — NOT 'copy the .app'. A genuinely portable bundle (macdeployqt, a real CFBundleIdentifier, a Developer ID signature) is a post-talk task and belongs in release.yml alongside item 15.

## Confirmed working (26)

- THE PRODUCT ACTUALLY PRESENTS. A full 10-slide talk was driven end to end through the real AppShell with real key events: every forward step had the projector pixels matching the controller's index, the Q&A blackout held for 1.5 s of no input, Left came back on slide 9, a typed jump to 10 landed, and the deliberate quit ended it. This is the first session where that is true.
- THE FULL SUITE IS GENUINELY GREEN AND FAST. ctest reports 100% tests passed, 178 tests, every single time. Clean from-scratch Release build in 9 s (2 s configure + 7 s compile of 55 targets) with ZERO compiler warnings; no-op rebuild 18 ms; suite 6.8-7.3 s serial and ~1.5 s at -j12. There is no speed-based excuse for skipping the gate before the talk. Reproduced from a fresh clone of main twice, and on Ubuntu CI.
- ASan + UBSan COMPLETELY CLEAN. 165 cases / 3722 assertions in pptv_tests and 9 / 32 in pptv_ui_tests, zero AddressSanitizer errors, zero UBSan runtime errors, zero SUMMARY lines. Caveat recorded honestly: LeakSanitizer is unsupported on this platform, so memory LEAKS were not checked at all.
- NO PRODUCT DATA RACE EXISTS, AND THIS WAS PROVEN RATHER THAN ASSUMED. TSan's 10-12 intermittent warnings were all traced to QSignalSpy's happens-before edge living inside uninstrumented QtCore: a pure-Qt control program with ZERO powerpoint-voice code reproduces the same race class, and the identical product path with a TSan-visible mutex+join edge produces zero warnings across all 8 error kinds. pptv_ui_tests under TSan: 0 warnings. Regression-guard separately ran the full AppShell wiring under key-spam with TSan: zero races.
- DETERMINISM. 14 consecutive full ctest runs green, byte-identical test-name lists, 3 runs at -j12 green, --repeat until-fail:5 over the whole suite green. The only non-determinism found needed --repeat until-fail:40 on a subset to surface and is a test-side assertion defect, not a product one.
- THE ARCHIVE-LEVEL LOADER DEFENCES ARE GENUINELY GOOD. 27 hostile .pptx families contained: zip bombs (1.2 GB declared, rejected in 0.0 s before any part was read), parts that lie about their size in both the central directory and the local header (four variants, all safe), zip-slip and path traversal including /etc/passwd (the loader never touches the filesystem for deck content at all, so there is no traversal primitive), duplicate part names, billion-laughs entity expansion and XXE (pugixml expands no custom entities and resolves no external ones), 100 MB of XML attributes, a 100 MB single text run (correctly truncated), non-images and absurd image dimensions, a nested archive as a media part, zero/negative/int64-max slide sizes, relationship cycles, and a 150,000-entry archive. Peak RSS never exceeded 448 MB in ANY test.
- THE C5 MEDIA READ-AMPLIFICATION FIX HOLDS, VERIFIED TWO WAYS. A 100 MB media part referenced by 2000 <p:pic> peaks at 149 MB RSS in 0.3 s (pre-fix this would have been ~200 GB resident), and the cumulative cap is charged exactly once per DISTINCT part. Independently, regression-guard built the pre-fix loader from git and measured 766 MB before vs 91 MB after on a 40-reference fixture — and confirmed the fix changes NOTHING for a legitimate deck: render output is byte-identical, all six PNG shasums match.
- THE VOICE COMMAND GRAMMAR RESISTS THE AUDIENCE. Every audience-sentence negative correctly yielded no command: 'let's move on to the next slide in our roadmap', 'the next slide shows our revenue', 'can you go back to the previous slide', 'any questions before we continue', 'we have about fifteen minutes left', 'that was slide ten', plus bare 'next', 'previous', 'pause', 'continue', 'resume', 'slide', empty and pure-punctuation input, and both integer-overflow attempts. All five real commands map correctly across case, whitespace, punctuation and number-word forms. BUG-17 single-word Q&A protection holds: bare 'pause', 'continue', 'resume', 'unpause', 'stop', 'go on', 'carry on' all produce nothing.
- BUG-16 RANGE REJECTION AND QUIT UNREACHABILITY ARE UNBREAKABLE. goto INT_MIN, -1, 0, 11 and INT_MAX on a 10-slide deck are all Rejected with the index unmoved. 200 consecutive requestHolding() calls (Esc auto-repeat) reach ConfirmQuit and stop with quitConfirmed still 0; 24 further commands under ConfirmQuit are all Suppressed; 200 real Esc key events through the window left it open. 30 Escapes in a row through the presenter harness never confirmed a quit. Esc never closes the window.
- THE TWO-STEP QUIT AND EVERYTHING AROUND IT. Ctrl+Shift+Q alone never quits. The quit prompt is genuinely visible (measured lit pixels), swallows a stray Right without moving the deck behind it, auto-dismisses to Holding at 10 s rather than to the deck, and Esc cancels it cleanly with the deck resuming on the next arrow. Close requests are refused while quitConfirmed is false.
- TM-018 HOLDS FOR NAVIGATION. A 50-key-press burst starting while pre-render was in flight took 0 ms of UI-thread time on a 40-slide deck, the controller clamped correctly at slide 40 (NoMove, not wraparound), and pre-render still completed afterwards. Jumping to an unrendered slide re-steers the worker and shows 'Rendering slide 35...' rather than a black screen. The UI thread never blocked on deck content.
- NO CROSS-DECK CONTENT LEAK. Specifically hunted: 8 rounds of switching away from a half-rendered 16-slide deck to a 3-slide deck, sampling the projector pixels and the whole raster cache every 5 ms across the transition — zero frames of deck-A content on screen, zero deck-A images in the new cache. A stale slideReady cannot survive rasters_.assign().
- THE C1 USE-AFTER-FREE IS GONE. Under ASan+UBSan, 80 arrow keys after pre-render completes plus a full quit: no heap-use-after-free, no SEGV, clean exit. Separately hammered 40 rounds of continuous key presses across the exact moment the worker self-destructs: no crash, 10/10 rasters every round. Only the narrow silent-invokeMethod residual remains (item 14).
- DECK CONTENT AND PATH CONTAINMENT IS OTHERWISE SEALED (one exception, item 13). grep across src/ for qDebug/qInfo/qWarning/qCritical/std::cout/printf/QFile WriteOnly/QTextStream/QSettings returns exactly ONE hit and it is a comment. LoadError::message is now dead code — never read anywhere in the product. Every error dialog shows a fixed string chosen by LoadErrorKind. Tested with parts deliberately named 'CONFIDENTIAL-Q3-BOARD-DECK.xml' and 'SECRET-ACQUISITION-TARGET-NAME.bin': both appear in LoadError::message and in render_preview output (a dev tool) and NEITHER reaches the app's UI. A launch path named /tmp/CONFIDENTIAL-Q3-BOARD-DECK.pptx was never echoed to stdout or stderr.
- THE APP NEVER WRITES TO DISK. Across every hostile deck driven through the real binary, the repository tree, ~/Library/Application Support, ~/Library/Preferences, ~/Library/Saved Application State, ~/Library/Logs, /var/tmp and the working directory were snapshotted before and after: zero files created by the application in any run.
- THE NOTICE VOCABULARY IS GENUINELY CLOSED. Across navigation, an out-of-range jump and a first-slide bounce, the strip only ever showed 'Slide 2', 'Deck has 10 slides', 'Already at the first slide' — integers and fixed strings only, no deck content and no path. An id plus two ints, as designed.
- THE QPixmap AMENDMENT (A3-1) HOLDS ABSOLUTELY. grep -rn QPixmap src/ returns two COMMENTS and no code. QImage crosses every queued connection.
- DECK-LOAD CANCEL SEMANTICS ARE CORRECT UNDER ADVERSARIAL TIMING. 400 instrumented trials of cancel-mid-parse: finished() emitted exactly once every run, loaded() emitted zero times every run (the late result really is discarded), the worker thread exited cleanly every time, 0 hangs. Two testers tried to make it hang and could not.
- ELEVEN PREVIOUSLY-FIXED AUDIT DEFECTS RE-VERIFIED AS GENUINELY FIXED and dropped from this report rather than silently omitted: C1 (worker UAF), C2 (0-slide deck no longer produces an unquittable fullscreen rectangle), C3 and C4 (deck path and freed-heap part name never reach a dialog), C5 (media amplification), H4 (quit prompt renders AND its 10 s timeout genuinely fires), H5 (window opens on the non-primary screen; Ctrl+Shift+D cycles correctly with fullscreen preserved), M1 (both the 3 s digit-staleness rule and the mode-clear fire at app level), M5 (a 250 MB file is rejected at 42 MB peak RSS, never read into memory), L3 (the running_ re-entrancy guard), and the F7a HIGH-1 and HIGH-2 fixes (undoJump respects the ConfirmQuit and Holding gates; the blackout survives a rejected command, a keyboard pause, voice-continue-while-paused and voice-nav-while-paused). H2 and H3 were the only two that did NOT hold — they are items 1 and 3.
- STATIC ANALYSIS FINDS NOTHING IN THE NEW PRESENTER CODE — a real negative result, not a skipped check. clang-analyzer-*, bugprone-*, cert-*, concurrency-*, misc-*, cppcoreguidelines-pro-type-member-init and -init-variables across all 11 files in src/present/ and src/ui/: zero findings except performance micro-suggestions and the already-known BUG-23 const std::move. The QThread / moveToThread / QImage-not-QPixmap discipline holds up under static analysis.
- CI ON main IS GREEN FOR THE REAL GATE, INCLUDING THE NEW WIDGET TESTS ON LINUX. Job `test` SUCCESS in 1m27s on ubuntu-24.04 with apt Qt 6.4.2, 178/178 in 3.77 s including all nine new 'U:' widget tests, with no QT_QPA_PLATFORM set anywhere in ci.yml — the offscreen plugin genuinely arrives through qt6-base-dev's hard dependencies. Verified empirically against the CI log, not inferred.
- GIT-LFS IS A NON-ISSUE. Nothing in CMakeLists.txt or src/ references vosk. Two independent clones built with the LFS objects deliberately absent (one via GIT_LFS_SKIP_SMUDGE, one with the lfs filter neutered entirely) cloned cleanly, configured, built and passed 178/178. CI checks out with lfs: false and never downloads the 76 MB.
- scripts/run-tests.sh, THE COMMIT GATE, STILL WORKS. From a fresh clone with only the documented environment: exit 0, clang-format check clean, 178/178.
- macOS LAUNCH EDGE CASES ARE CLEAN. The legacy Finder '-psn_0_xxxxx' argument does not trip QCommandLineParser — Qt strips it and the app starts normally. Unknown options exit rc=1 with a single-line message rather than hanging or crashing. --version prints 'powerpoint-voice 0.1.0'.
- THE PRE-RENDER PREVENT CAP DIVERTS OVER-CAP SLIDES. 3000 text shapes on one slide (over RenderCaps::maxShapesPerSlide of 2000, under the loader's 5000): the app survived with no hang and no crash, and the slide was diverted to a placeholder without entering the renderer. Recorded honestly as a negative result — the placeholder image itself could not be observed under offscreen QPA.
- THE REPOSITORY IS UNTOUCHED. All five testers confirmed git status --porcelain empty; all builds and scratch artifacts went to the scratchpad. The Confidential real deck was never located, opened, read, copied or rendered by anyone — only tests/fixtures/*.pptx and generated hostile fixtures were used.
