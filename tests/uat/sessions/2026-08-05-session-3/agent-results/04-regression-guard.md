# Agent Result — regression-guard (UAT Session 3)

**Summary:** All 11 previously-fixed defects were re-run against their original reproductions. 9 hold: C1 (no UAF under ASan after 80 keys post-pre-render), C2 (0-slide deck refused, no fullscreen window), C3 + C4 (LoadError::message is now dead code — `grep` shows it is never read; the hostile over-cap fixture yields only the fixed string, ASan-clean), C5 (766 MB → 91 MB peak RSS on a 40×17 MB media fixture, and render output is BYTE-IDENTICAL before/after on a legitimate repeated-image deck), H4 (prompt renders + 10 s timeout proven), H5 (with two screens configured the window opens on the non-primary screen and Ctrl+Shift+D cycles correctly), M1 (both the 3 s staleness rule and the mode-clear fire), M5 (250 MB file → 42 MB peak RSS), L3 (running_ guard present). All F7a fixes hold: undoJump is Suppressed under ConfirmQuit and leaves Holding→Presenting rather than moving behind the blackout; rejected commands, pause, and voice-continue-while-paused all leave the blackout up; quit is unreachable from 200 Esc presses and 24 commands; BUG-16 rejects INT_MIN/-1/0/N+1/INT_MAX; BUG-17 rejects bare "pause"/"continue"/"resume"/"unpause"/"go on". 178 ctest tests, 3722 ASan+UBSan assertions and 3722 TSan assertions are all green with zero races.

TWO of the fixes do NOT hold. H3 (privacy blackout) is STILL BROKEN by a different route the fix did not close: refresh() blanks correctly, but AppShell::onSlideReady calls showSlide() with no mode check, so the pre-render worker paints the confidential deck straight back onto a blanked projector — reproduced in under 25 ms on ordinary text decks, with 56.2% of the projector confirmed as deck pixels while the controller is still in Mode::Holding. The same path erases the quit prompt, re-opening H4's "frozen app". H2 (bounded shutdown) is STILL BROKEN: terminate() does not stop a CPU-bound QPainter loop on macOS, so quitting during a >5 s slide reproduces the exact `QThread: Destroyed while thread is still running` qFatal the fix claimed to remove (SIGABRT, signal 6, at exactly 5000+1000 ms). Worse, when the cancel DOES land at a pthread cancellation point it orphans a Qt-internal mutex: I captured a live `sample` of a permanent two-thread deadlock. One new residual of the C1 fix: QPointer's check-then-use is not atomic across threads, and showSlide() was caught calling invokeMethod on a partially-destroyed worker in 7 of 120 single-load runs.

Note for triage: voice is not wired into AppShell at all (only CommandSource::Keyboard appears at app_shell.cpp:138), so the voice-related findings below are latent for 2026-08-10 and become live the moment F5 lands. BUG-21/22/23 were observed but are not re-reported.

## Findings (7)

### [SEV-1] H3 STILL BROKEN — the pre-render worker repaints the confidential deck onto the blanked projector (and erases the quit prompt)

**Repro:** The H3 fix made refresh() mode-aware, but AppShell::onSlideReady (src/ui/app_shell.cpp:211-220) calls showSlide() with NO mode check, and showSlide() (line 270-286) is the only writer of the surface image. Any slideReady for the currently-selected slide that lands while Mode is Holding or ConfirmQuit repaints the deck.

  python3 /private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/mkfix.py /tmp/many60.pptx many 60 30
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe h3blackout /tmp/many60.pptx 60

(probe source: <scratchpad>/repo/tools/shell_probe.cpp; it drives the real AppShell with real QKeyEvents and uses SlideSurface::lastPaintedRect(), which is empty exactly when no raster is drawn.)

Sequence: jump to a slide the renderer has not reached yet -> press Esc (Mode::Holding, surface confirmed blank) -> wait.

Observed on a plain 60-slide text deck:
  after jump to 60: surfaceShowingSlide=0 (raster not ready, test valid)
  after Esc: surfaceShowingSlide=0
  LEAK at t=0 ms after Esc: the deck is back on the projector while Mode::Holding
  PIXELS nonBlack=360000 of 640000 (56.2% of the projector is deck content)
  MODEPROBE one-Esc-then-chord closed the window=1  (1 => mode was STILL Holding at leak time)

Identical result on the 200-slide deck. Pixel evidence saved to <scratchpad>/leak-holding.png.

The quit prompt is destroyed the same way:
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe h3confirm <scratchpad>/fix/slow12b.pptx 12
  -> LEAK at t=2200 ms: the deck replaced the quit prompt

And the plainest stage flow of all — launch with the deck, hit Esc to blank while the room fills, wait — leaks with no navigation at all:
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe openblank <scratchpad>/fix/slow12b.pptx
  -> LEAK at t=1150 ms: slide 1 painted itself onto the blanked projector

**Impact:** Esc is the ONLY privacy control the product has (TM-002/012/019, Bible §8). Karl blanks the projector — someone walks in, a question comes from the room, he steps away — and one to three seconds later the Confidential slide paints itself back onto the wall with no notice, no strip text, and nothing on screen to tell him it happened. He is facing the audience, not the screen. The window is exactly the pre-render period: his 10-slide deck is ~316 MB of rasters (~31.6 MB/slide, i.e. 4K), so pre-render runs for roughly 1-5 s after the deck opens — precisely the moment a presenter blanks the screen while setting up. The same defect wipes the quit prompt mid-quit, leaving a window that swallows every key while showing a slide: H4's "frozen app" symptom, restored. The audit recorded H3 as Fixed on the strength of refresh() alone; the raster path was never gated.

**Fix:** Gate the surface write on the mode. Minimal correct change in src/ui/app_shell.cpp:onSlideReady — route through the already-mode-aware refresh() instead of showSlide():

    void AppShell::onSlideReady(int index, QImage image, bool) {
        if (index >= 0 && index < (int)rasters_.size())
            rasters_[(std::size_t)index] = image;
        if (index == controller_.currentIndex0Based())
            refresh();            // was: showSlide(...)
    }

Belt-and-braces, make showSlide() itself refuse to write behind a gate, since it is the single writer of the surface image:

    void AppShell::showSlide(int index1Based) {
        if (!window_ || index1Based < 1) return;
        if (controller_.mode() != Mode::Presenting) return;   // NEVER paint behind the blackout or the prompt
        ...

Add a regression test asserting that a slideReady delivered while Mode::Holding leaves SlideSurface::lastPaintedRect() empty — that assertion is what the F7b suite is missing, and it is cheap because lastPaintedRect() is already public.

### [SEV-2] H2 STILL BROKEN — quitting during a slow slide aborts with the exact qFatal the fix claimed to remove; terminate() can instead orphan a Qt mutex and wedge the app

**Repro:** terminate() on macOS is pthread_cancel, which cannot interrupt a CPU-bound QPainter loop (no cancellation point), so teardownWorkers() (src/ui/app_shell.cpp:62-90) falls through wait(5000) -> terminate() -> wait(1000) -> deleteLater() (a no-op after the event loop has exited) -> ~QObject deletes a still-running QThread.

  python3 <scratchpad>/mkfix.py /tmp/slow1.pptx bomb 1 1990 2000     # 1 slide, 1990 shapes (UNDER the 2000 cap), 13.3 s to render
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe teardown /tmp/slow1.pptx; echo $?

Observed:
  EXITING at t=1357 ms (AppShell destructor runs teardownWorkers now)
  QThread: Destroyed while thread '' is still running
  EXIT STATUS = 134  -> KILLED BY SIGNAL 6 (SIGABRT)

The process died 6.00 s after EXITING — exactly wait(5000) + wait(1000), proving BOTH waits timed out and terminate() did nothing. Control runs on decks that render fast (good10, 12 slides at 1.5 s each) exit 0 and print TEARDOWN-DONE, confirming the trigger is a single slide exceeding 5 s.

The terminate() the fix ADDED has a second failure mode. When the cancel does land on a cancellation point it unwinds the thread while it holds a Qt-internal mutex:

  python3 <scratchpad>/mkfix.py /tmp/slow7.pptx bomb 2 1000 2000
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe hammer /tmp/slow7.pptx 2 &
  sample $! 3 -f /tmp/sample.txt

Captured stacks (full sample at <scratchpad>/sample.txt) — a permanent deadlock:
  main thread:   AppShell::openDeck -> QObject::connectImpl -> QObjectPrivate::connectImpl -> QBasicMutex::lockInternal -> __ulock_wait2
  worker thread: PreRenderWorker::renderOne -> slideReady -> QCoreApplication::postEvent -> write() -> _pthread_exit_if_canceled
                 -> [cancellation cleanup] sendPostedEvents -> ~PreRenderWorker -> ~QObject -> QBasicMutex::lockInternal -> __ulock_wait2
Both blocked on the same orphaned QBasicMutex. Killed after 300 s, still wedged.

**Impact:** The abort is reproduced on the product path: Esc, Esc, Ctrl+Shift+Q while any single slide is still rendering after 5 s and the app dies with SIGABRT — macOS shows a "powerpoint_voice quit unexpectedly" crash dialog in front of the room. BUG-21 is logged precisely because a legal, under-cap slide can take minutes, so this is not a hostile-input-only case. The deadlock variant is worse and I demonstrated the mechanism rather than the product path: a wedged process leaves the fullscreen window frozen on the projector with the deck (or the blackout) still displayed and no way out except Force Quit — which is the C2 failure the audit just closed. This escalates to SEV-1 for Karl specifically if any slide in his real deck takes more than 5 s to rasterise; measure that before the 10th (time render_preview on the real deck and divide by slide count).

**Fix:** Do not use terminate(); it is unsafe by construction and cannot be made safe here. Make wait() always succeed instead:

1. Make the render itself cancellable so the worker stops mid-slide, not just between slides. Pass the worker's `cancelled_` atomic into SlideRenderer::render and check it once per shape in the element loop, returning a null QImage on cancel (renderOne already handles a null image via the placeholder path). With that, wait(5000) always returns true and the terminate() branch becomes dead.
2. Delete the QThread directly after a successful wait rather than deleteLater() — teardownWorkers() runs from ~AppShell after the event loop has exited, so the posted DeferredDelete is never processed and the current code is relying on ~QObject's child cleanup by accident.
3. As a belt-and-braces last resort, if wait() ever does fail, DETACH rather than destroy: `t->setParent(nullptr);` and leak the thread deliberately. Leaking a thread at process exit is free; destroying a running QThread is a guaranteed qFatal.

Add a test that opens a deck whose slide render blocks on a test-controlled latch, calls the teardown path, and asserts it returns within a bound with the process alive.

### [SEV-3] C1 residual — the QPointer fix does not close the window it claims to; showSlide() calls invokeMethod on a partially-destroyed worker

**Repro:** The audit's claim is that "QPointer self-nulls, so every `if (worker_)` guard in this file becomes truthful" (src/ui/app_shell.hpp:56-61). That is false across threads. The worker is destroyed ON THE WORKER THREAD (QThread::finished -> deleteLater is a direct connection because the receiver lives there; QThreadPrivate::finish then drains DeferredDelete), while `if (renderWorker_)` and the invokeMethod that follows it run on the GUI thread (src/ui/app_shell.cpp:281-285). Check-then-use is not atomic, and QPointer's weak ref is only cleared late inside ~QObject.

120 separate processes, ONE openDeck each — the exact product path — with arrow keys spammed across the instant pre-render completes:

  for i in $(seq 1 120); do QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe hammer <scratchpad>/fix/good10.pptx 1; done | grep -c 'No such method'

Observed: 7 / 120 processes printed
  QMetaObject::invokeMethod: No such method QObject::setCurrentIndex(int)

That message can only be produced by reading the metaObject of an object whose derived destructor has already run (the vptr has been downgraded to QObject's) but whose ~QObject has not yet cleared the QPointer. Reproduced in Release, in the ASan+UBSan build, and in the TSan build. TSan reports ZERO data races on the same run — correct, because the QPointer weak-ref is atomic; this is a lifetime/ordering bug, not a data race, which is why the audit's TSan-clean result did not catch it.

**Impact:** The observed consequence on stage is benign-to-moderate: the re-steer is silently lost, so the renderer does not learn which slide the presenter jumped to and keeps rendering in its old order — the presenter sits on "Rendering slide N..." longer than necessary. But it is an unsynchronised cross-thread dereference of an object in the middle of destruction, i.e. undefined behaviour on the live path, and QCoreApplication::postEvent is being called against a receiver whose thread data is being torn down. It fired in ~6% of loads under key pressure; a real presenter presses far fewer keys in that window, so the practical rate is much lower. The reason to fix it is that the audit closed C1 on a guarantee that does not exist, so the next change in this area will assume a safety property that is not there.

**Fix:** Stop using a raw/QPointer dereference for a cross-thread call. Use a signal, which Qt emits under its own connection lock and which handles receiver destruction correctly:

  // app_shell.hpp
  signals: void currentIndexChanged(int index0Based);

  // openDeck(), alongside the other connects
  connect(this, &AppShell::currentIndexChanged, renderWorker_, &PreRenderWorker::setCurrentIndex);

  // showSlide(), replacing the invokeMethod block
  emit currentIndexChanged(index1Based - 1);

That removes the invokeMethod-by-name (which is also why the failure was silent — a string lookup that misses only warns) and removes the check-then-use entirely. Alternatively, own the worker with a std::shared_ptr and drop the last reference only on the GUI thread from a queued `finished` handler, so the object cannot die while the GUI thread holds it.

### [SEV-3] Notices are never cleared — NoticeClass::Transient is declared, set on every notice, and read by nothing

**Repro:** grep -rn 'NoticeClass|\.cls' src/ | grep -v 'Notice{'  ->  only the enum declaration (notice.hpp:29) and the struct member (notice.hpp:39). No consumer anywhere. noticeForRole() ignores it, applyResult() ignores it, NoticeStrip has no timer.

  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe notice <scratchpad>/fix/good10.pptx

Sequence: on a 10-slide deck type 9, 9, Enter (a rejected out-of-range jump), then touch nothing.

Observed:
  notice right after a rejected jump: [Deck has 10 slides]
  notice 8 s later, no further input:  [Deck has 10 slides]

AppShell::lastNotice_ (app_shell.cpp:294) is overwritten only by the next command, so the message persists indefinitely until Karl presses another key.

**Impact:** One mistyped slide number puts "Deck has 10 slides" in the strip at the bottom of the projector and leaves it there for the rest of the talk if Karl then speaks for ten minutes without touching the keyboard. Same for "End of deck — slide 10 of 10" and "Already on slide 3". It discloses nothing (the Notice vocabulary is genuinely closed — verified) and blocks nothing, but it is audience-facing text that the design explicitly classified as Transient and then never implemented. Overlaps BUG-19's "cosmetic notice defects (unused args)" but is a distinct, user-visible behaviour rather than a dead parameter.

**Fix:** Honour the class in AppShell. Add a single-shot timer alongside tick_:

  // applyResult(), after setting lastNotice_
  noticeExpiryMs_ = (r.notice.cls == NoticeClass::Transient && !lastNotice_.isEmpty())
                        ? clock_.elapsed() + kNoticeTransientMs   // ~4000
                        : -1;

  // in the existing 250 ms tick_ lambda
  if (noticeExpiryMs_ > 0 && clock_.elapsed() >= noticeExpiryMs_) {
      lastNotice_.clear();
      noticeExpiryMs_ = -1;
      refresh();
  }

Sticky notices (DeckEmpty, Paused) keep noticeExpiryMs_ == -1 and stay up, which is the documented intent.

### [SEV-3] Latent gate gaps around the blackout and undo — both become live the moment voice is wired in

**Repro:** Reproduced with <scratchpad>/repo/tools/ctrl_probe.cpp (links pptv_core, drives PresentationController directly):

  <scratchpad>/pbuild/ctrl_probe

(a) undoJump() takes no CommandSource and no `paused` flag, so the pause gate structurally cannot see it:
  voice NextSlide while paused:                    outcome=Suppressed slide=40
  undoJump (no source/paused parameter exists):    outcome=Moved      slide=1
The HIGH-1 fix added the ConfirmQuit and Holding gates to undoJump but not the pause gate — the header still calls dispatch() "the only code that computes a slide index", and undoJump is a second one with a different gate set.

(b) The blackout un-hold in dispatch() (presentation_controller.cpp:117-120) is source-blind. With the room unpaused:
  voice next slide, NOT paused      -> outcome=Moved mode=Presenting slide=21  *** BLACKOUT DISMISSED ***
  voice go-to-slide 7, NOT paused   -> outcome=Moved mode=Presenting slide=7   *** BLACKOUT DISMISSED ***
All the paths HIGH-2 named are correctly held (rejected command, pause, voice-continue-while-paused, voice-nav-while-paused all leave the blackout up).

Neither is live today: grep -rn 'CommandSource::' src/ returns exactly one product call site, app_shell.cpp:138, hard-coded to CommandSource::Keyboard. RecognizerController is never instantiated by AppShell, and undoJump has no product caller at all (only tests).

**Impact:** Nothing on 2026-08-10 — the shipped app is keyboard-only and nothing calls undoJump. But the blackout is raised precisely when the room is listening and the mic is live and unpaused: someone walks in, Karl hits Esc, and under F5 a single audience utterance of "next slide" both reveals the Confidential deck and advances it. That is the TM-002/019 scenario verbatim, and HIGH-2's fix does not cover it because the un-hold never looks at the source. Likewise, wiring a voice "undo" in F5 hands the audience an index-computing entry point that the Paused state cannot suppress. Worth closing now while the funnel is small rather than after F5 has added callers.

**Fix:** (a) Give undoJump the same signature shape as dispatch so no call site can omit the gate:
    DispatchResult undoJump(CommandSource src, bool paused);
  and run the same preamble — Suppressed under ConfirmQuit (already there), Rejected on an empty deck, Suppressed when `paused && src == CommandSource::Voice`.

(b) Make the un-hold source-aware in presentation_controller.cpp:117:
    if (mode_ == Mode::Holding && src == CommandSource::Keyboard && isNavigation(cmd.type) &&
        (r.outcome == Outcome::Moved || r.outcome == Outcome::NoMove)) {
        mode_ = Mode::Presenting;
    }
  The blackout is a deliberate act by the presenter; only the presenter's own audited input device should end it. Voice navigation while Holding should be Suppressed and leave the blackout up. Assert both in tests.

### [SEV-4] The blackout hint on the audience-facing projector is operator instruction text, and it is factually wrong

**Repro:** src/present/notice.cpp:35-36:
  case NoticeId::HoldingHint:
      return QStringLiteral("Presentation paused — press Esc again to exit, any key to resume");

src/ui/app_shell.cpp:230-232 paints it on the fullscreen (audience) window and asks for NoticeRole::Operator, even though applyResult() deliberately uses NoticeRole::Audience for everything else per audit M2. noticeForRole ignores the role for this id, so the request has no effect and the operator string reaches the audience either way.

"any key to resume" is false: per presentation_controller.cpp:117-120 only an ACCEPTED navigation un-holds. Verified with <scratchpad>/pbuild/ctrl_probe — P (pause), a rejected out-of-range jump, and voice-continue all leave Mode::Holding, and digits/Enter never reach dispatch at all.

**Impact:** During the privacy blackout the projector shows the room a line of presenter instructions telling them how to un-blank the screen. It leaks no deck content (the Notice vocabulary is genuinely closed — verified: zero qDebug/qWarning/file writes in src/, and LoadError::message is never read). It also mis-instructs Karl: if he taps P or a digit expecting to resume, nothing happens and he is left pressing keys at a black screen in front of the room. Small, but it is on the audience screen at the single most sensitive moment the product has.

**Fix:** Split the string by role and make the audience version content-free, e.g. Audience -> QStringLiteral("Presentation paused") (or empty), Operator -> QStringLiteral("Paused — Esc again to exit, or an arrow key to resume"). Then correct the caller in app_shell.cpp:231 to NoticeRole::Audience to match applyResult(). This is the same role-policy gap already logged as BUG-19 ("the audience/operator split is implemented for only 2 of 11 notice ids"); HoldingHint is now a live caller of it, so it is no longer purely theoretical.

### [SEV-4] The C2/C3 fixes claim they "return to the start view", but on the only real launch path there is no start view and the app exits

**Repro:** main.cpp:24-30 calls openDeck() for a CLI deck WITHOUT ever calling showStart(), so start_ is nullptr and the `if (start_) start_->show();` guards at app_shell.cpp:114-116 and 127-129 do nothing. Reproduced with a probe that mirrors main.cpp exactly (no showStart()):

  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe cli <scratchpad>/fix/zero.pptx
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe cli /Users/karl/CONFIDENTIAL-Q3-BOARD-DECK.pptx
  QT_QPA_PLATFORM=offscreen <scratchpad>/pbuild/shell_probe cli <scratchpad>/fix/c4_overcap.pptx

All three print the correct fixed dialog string and then:
  WINDOW present=0
  VISIBLE-TOPLEVEL-COUNT 0

With quitOnLastWindowClosed at its default, dismissing the dialog closes the last window and the application exits. Separately, grep -rn 'openDeck|QFileDialog' src/ shows openDeck has exactly one caller (main.cpp:27) and StartView has no open control at all — QFileDialog is #included in app_shell.cpp:4 and never used.

**Impact:** The safety property C2 existed to restore is intact and verified — there is no unquittable fullscreen window, which was the whole point. But the recovery the audit describes does not happen: if Karl's deck fails to open two minutes before the talk (wrong path, a deck PowerPoint saved in a form the loader rejects), he gets a one-line dialog and the application vanishes. His only route back is a terminal and the exact CLI incantation. The StartView he would land on if showStart() had been called is a dead end anyway — it says "No deck loaded" and has no way to open one. Not a regression; the fix strictly improved on an unquittable black rectangle. Flagged because the audit text asserts a recovery that the shipped path does not provide.

**Fix:** Two small changes, either of which removes the stage risk:
(1) In main.cpp, always call shell.showStart() before openDeck() so the error paths land somewhere rather than exiting, and set QApplication::setQuitOnLastWindowClosed(false) while a shell exists.
(2) Give StartView a single "Open deck…" button wired to a QFileDialog::getOpenFileName -> AppShell::openDeck. The QFileDialog include is already sitting unused in app_shell.cpp. That also removes the CLI-only constraint before the talk.
If neither lands before 2026-08-10, the mitigation is operational: dry-run the exact deck with the exact command line, and keep the command in the speaker notes.

## Could not break

- C1 (worker use-after-free -> SEGV on the first key after pre-render) — VERIFIED. Under the ASan+UBSan build I sent 80 arrow keys after waiting 6 s for pre-render to complete, plus a full Esc/Esc/Ctrl+Shift+Q quit: no heap-use-after-free, no SEGV, clean exit. `QT_QPA_PLATFORM=offscreen ASAN_OPTIONS=detect_leaks=0 <scratchpad>/asan/shell_probe c1 <scratchpad>/fix/good10.pptx`. The QPointer does prevent the guaranteed dangle the audit described; it leaves only the narrow cross-thread race reported separately as SEV-3.
- C2 (0-slide deck -> unquittable fullscreen) — VERIFIED. A hand-built .pptx with a valid presentation.xml and an EMPTY <p:sldIdLst> is refused before any window is created: WINDOW present=0, dialog reads "That file contains no slides.", and on the StartView path the presenter is returned to a visible window. No fullscreen surface is ever shown, so the Force-Quit-only state is genuinely gone. `<scratchpad>/pbuild/shell_probe smoke <scratchpad>/fix/zero.pptx`.
- C3 (deck path in a dialog) — VERIFIED, structurally. I sniffed every QMessageBox the app raises (title, text, informativeText, detailedText) for a missing file, a 0-slide deck, an over-cap hostile archive and a 250 MB file. Every field contains only the fixed describeLoadError() string; the path never appears. `grep -rn '\.message' src/` shows LoadError::message is never read anywhere in the product — the channel is dead code, not merely unused. I could not construct any input that put a path on a display.
- C4 (st.name read after zip_close) — VERIFIED. Built a hostile .pptx declaring a 130 MB part (over the 128 MB per-part cap) with a 200-character distinctive name chosen so any freed-heap garbage would be unmistakable. Under ASan+UBSan: no use-after-free, and the dialog shows only "That deck expands to too much data to open safely." The diff against pre-fix confirms the name is now copied into a QString before zip_close. `ASAN_OPTIONS=detect_leaks=0 <scratchpad>/asan/shell_probe smoke <scratchpad>/fix/c4_overcap.pptx`.
- C5 (media read amplification) — VERIFIED, with a measured before/after. Built the pre-fix loader from git (`git show 11d583a:src/loader/deck_loader.cpp`) into a parallel tree and ran both against a fixture with 40 <p:pic> references to one 17.3 MB media part. Peak RSS: 766,099,456 bytes BEFORE vs 90,963,968 AFTER — the 40x amplification is gone. I could not find an iterator-invalidation or aliasing bug in the QHash cache; the iterator is always taken from the insert() that may rehash, and it is re-derived each loop iteration.
- C5 fix does NOT change loader behaviour for a legitimate deck — VERIFIED explicitly, as asked. Ran render_preview from both trees over a legitimate 6-slide deck with the same image repeated 3x per slide: `diff -r` reports the outputs identical and all six PNG shasums match byte-for-byte (09f1eaf0..., a7973508..., 08c26d81..., 987a1aef..., cff8c985..., 51290f77...). The only behavioural delta I could identify is that mediaBytes is a second independent 1 GB budget on top of the central-directory total, so a deck can consume up to ~2 GB rather than 1 GB — not a live-talk risk and not worth a finding.
- H4 (invisible quit prompt that never timed out) — VERIFIED, both halves. The prompt blanks the surface and renders (Holding: showingSlide=0, ConfirmQuit: showingSlide=0); a stray Right arrow in ConfirmQuit is swallowed and leaves the window open. The 10 s timeout genuinely fires: after Esc,Esc and an 11 s wait, ONE Esc plus Ctrl+Shift+Q closed the window — which is only possible if the mode had already fallen back to Holding. Had the timeout not fired, that Esc would have been CancelQuit and the chord would have been refused. `<scratchpad>/pbuild/shell_probe h4 <scratchpad>/fix/good10.pptx`.
- H5 (projector routing) — VERIFIED with two real Qt screens, using the offscreen plugin's multi-screen config: `QT_QPA_PLATFORM="offscreen:configfile=screens.json"` with a 1512x982 primary "LaptopPanel" and a 1920x1080 non-primary "Projector". The window opened on Projector at 1920x1080@1512,0; Ctrl+Shift+D moved it to LaptopPanel and a second press returned it to Projector, fullscreen preserved both times. I also checked the adjacent risk that the pre-render target might be sized from the wrong screen: renderTargetPolicy() takes the largest device-pixel size across ALL screens, so the raster is correct regardless of which screen the window lands on.
- M1 (typed-number staleness and mode-clear both dead) — VERIFIED at the app level, not just the unit. Typed '1', waited 4 s (> kDigitStaleMs 3000), typed '0' and Enter on a 10-slide deck: the notice was "Deck has 10 slides" (slide 0 rejected), NOT a jump to slide 10 — the stale digit was discarded. Same result for the mode-clear path (type '1', Esc, resume, type '0', Enter). PresentationWindow::nowMs() feeds a real monotonic QElapsedTimer and onModeChanged() is called on every mode transition. `<scratchpad>/pbuild/shell_probe m1 <scratchpad>/fix/good10.pptx`.
- M5 (readAll before the size cap) — VERIFIED. A 250 MB file (over the 200 MB maxFileBytes) is rejected with "That deck is too large to open safely." at a peak RSS of 42,287,104 bytes — the file was never pulled into memory. The gate at deck_load_worker.cpp:70 precedes the QFile::readAll() used for the content hash.
- L3 (start() re-entrancy) — VERIFIED by inspection plus a green suite; the running_ guard is present at pre_render_worker.cpp:103-110 and reset at 141, and the cancelled-break path still falls through to `running_ = false; emit finished();` so a cancelled run cannot strand the guard. I could not construct a re-entrant call that reset done_/emitted_ mid-run.
- F7a HIGH-1 (undoJump respecting the gates) — VERIFIED. Under ConfirmQuit: outcome=Suppressed, slide unchanged at 40, mode still ConfirmQuit. Under Holding: outcome=Moved to slide 1 AND mode moved to Presenting, so it never repositions the deck behind the blackout. Reproduced directly against PresentationController with <scratchpad>/repo/tools/ctrl_probe.cpp. (The missing pause gate is a separate signature gap, reported as SEV-3.)
- F7a HIGH-2 (blackout dismissal) — VERIFIED for every path the audit named. With the blackout up on slide 20: a REJECTED go-to-slide 999 leaves it up; keyboard pause leaves it up; a voice "continue presentation" while paused leaves it up; voice navigation while paused is Suppressed and leaves it up. Only an accepted navigation un-holds. The residual is that the rule is source-blind, reported separately.
- BUG-17 (one-word un-pause during Q&A) — VERIFIED. Via command_probe: bare "pause", "continue", "resume", "unpause", "stop", "go on", "carry on" and bare "next" ALL produce no command. Only the full object forms fire: "pause presentation" -> PausePresentation, "continue presentation" and "resume presentation" -> ContinuePresentation, "next slide" -> NextSlide, "go to slide 3" -> GoToSlide(3). An audience member cannot re-arm navigation with a single conversational word.
- BUG-16 range rejection and quit unreachability — could not be broken, consistent with the F7a audit. goto INT_MIN, -1, 0, 11 and INT_MAX on a 10-slide deck are all Rejected with the index unmoved; 1 -> NoMove; 10 -> Moved. 200 consecutive requestHolding() calls (Esc auto-repeat) reach ConfirmQuit and stop — quitConfirmed stays 0 — and 24 further commands under ConfirmQuit are all Suppressed with quitConfirmed still 0. 200 real Esc key events through the window left it open.
- Project Bible §8 / TM-012 / TM-013 content containment — could not be broken. `grep -rnE 'qDebug|qInfo|qWarning|qCritical|qFatal|std::cout|printf|QFile.*WriteOnly|QTextStream|QSettings' src/` returns exactly one hit, and it is a COMMENT. Nothing in the product logs, prints, or persists anything. Combined with LoadError::message being dead and the Notice vocabulary being an enum id plus two ints, I could find no channel by which slide text, a file path, or heard speech reaches a log or a persisted artefact.
- The QPixmap amendment (A3-1) — holds absolutely, re-verified. `grep -rn QPixmap src/` returns two COMMENTS and no code. QImage crosses every queued connection.
- Sanitizer parity with the audit — 178/178 ctest green; 165 test cases / 3722 assertions green under ASan+UBSan; 165 / 3722 green under ThreadSanitizer with ZERO race warnings, including a TSan run of the full AppShell wiring under the key-spam hammer. The audit's TSan result reproduces exactly.
- BUG-21, BUG-22 and BUG-23 were observed but are NOT re-reported per instruction. For the record BUG-21 is worse than a theoretical risk and is load-bearing for the H2 finding: a single slide with 1990 shapes — UNDER the 2000-shape cap and containing zero text runs — took 13.3 s to rasterise (`render_preview` on a generated fixture), which is exactly what pushes teardownWorkers past its 5 s wait.
