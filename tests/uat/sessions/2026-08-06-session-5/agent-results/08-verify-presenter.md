All eight findings re-tested independently against the live tree. Verdicts below.

---

## Environment caveat (affects nothing, but must be stated)

The working tree is **not** clean at `079de1a`. A concurrent session has staged a model-path fix:

```
CMakeLists.txt                  | 20 ++    (stage libvosk + model into the .app)
src/command/vosk_recognizer.cpp | 18 ++    (new resolveModelDir())
src/command/vosk_recognizer.hpp |  9 ++
src/ui/app_shell.cpp            |  2 +-    (prepareRecognizer(resolveModelDir()))
```

None of it touches the grammar, the ownership order, the gates, the matcher or the leak, and every line number cited by the tester still resolves correctly. `ctest`: **253/253 green** before and after my work. I created/edited/deleted no project file and never opened the confidential deck.

---

## Verdict table

| # | Finding | Verdict | Their SEV | Agreed SEV |
|---|---|---|---|---|
| a | Voice command defeats the privacy blackout | **CONFIRMED** | SEV-1 | **SEV-1** |
| b | 87% of natural spoken commands do not work | **CONFIRMED** (and worse) | SEV-1 | **SEV-2** (SEV-1 priority) |
| c | Quitting with voice armed corrupts memory | **CONFIRMED**; "second deck" sub-claim **REFUTED** | SEV-2 | **SEV-2** |
| d | Pause invisible; keyboard "P" escape hatch absent | **CONFIRMED** | SEV-2 | **SEV-2** |
| e | Presenter never told voice failed to arm | **CONFIRMED**; modal-on-projector half **REFUTED** | SEV-2 | **SEV-3** |
| f | `vosk_recognizer.hpp` safety claim false as built | **CONFIRMED** | SEV-3 | **SEV-3** |
| g | `~AppShell` leaks the window with a dangling controller | **OVERSTATED** (leak real, exposure not reachable) | SEV-3 | **SEV-4** |
| h | Mistyped file mid-talk puts the picker in front of the room | **REFUTED** (unreachable) | SEV-3 | **SEV-4** |

---

## (a) Blackout defeated by voice — CONFIRMED, SEV-1

Reproduced against the **real, unmodified `AppShell`** (real `PresentationController`, real `RecognizerController`, real `PresentationWindow`, real armed microphone). Only the recognised *text* is injected, by activating the production `VoicePipeline::phraseHeard` signal through the meta-object system — the same call the audio thread makes. "Deck is on the wall" is measured as `SlideSurface::lastPaintedRect()` being non-empty.

```
window=0xa9f00c6c0 voiceIsRunning=1 reason=""
  after keyboard Right          painted=10f42196 strip="Slide 2"
  after Esc (blackout)          painted=BLANK    strip=""
  after VOICE 'next slide'      painted=c4d231e3 strip="Slide 3"
  >>> DID A VOICE COMMAND UN-BLANK THE PROJECTOR? YES
  Esc again (blackout #2)       painted=BLANK
  after VOICE 'go to slide seven' painted=f7df1e2d strip="Slide 7"
```

Cited lines verified: `presentation_controller.cpp:117-120` is source-blind; `app_shell.cpp:90` passes `CommandSource::Voice`; Esc does not pause the recogniser. The deferral rationale at `tests/uat/sessions/2026-08-05-session-3/agent-results/04-regression-guard.md:171` is quoted accurately and its stated precondition ("RecognizerController is never instantiated by AppShell") is now false.

**Smallest fix — and the tester's one-liner is not sufficient.** Adding `src == CommandSource::Keyboard` to the un-hold condition stops the *reveal* but the `NextSlide` case still runs `++current_` (outcome `Moved`), so the deck silently advances behind the blackout and the presenter resumes on a different slide. The smallest *correct* fix is to suppress voice navigation while Holding, alongside the existing pause gate at `presentation_controller.cpp:46`:

```cpp
if (src == CommandSource::Voice && isNavigation(cmd.type) &&
    (paused || mode_ == Mode::Holding)) {
    r.outcome = Outcome::Suppressed;
    r.notice = Notice{NoticeId::Paused, 0, 0, NoticeClass::Sticky};
    return r;
}
```

`undoJump()` (`presentation_controller.cpp:175`) confirmed to take no source/paused parameter and to have no product caller.

## (b) Natural phrasings fail — CONFIRMED with an independent corpus, and it is worse than reported

My own corpus (voices **Alex / Daniel / Karen** — the tester used Samantha; my own phrase list; my own harness), 16 kHz mono s16 from `say`, driven through the production `prepareRecognizer` → `VoskEngine::start` → `feed` → `matchCommand`, matching **per finalised utterance** exactly as `RecognizerController` does:

```
bare, isolated commands   : 24/24 produced a command (100%)
natural phrasings         :  9/70 produced a command  (13%)  -> 87% failure
```

87% independently reproduced (their 34/39 = 87.2%). Samples:

```
okay next slide     -> heard "go next slide" / "go to next slide" / "thirteen next slide" -> NO COMMAND
alright next slide  -> heard "hundred next slide" / "four five next slide"                -> NO COMMAND
next slide please   -> heard "next slide previous"                                        -> NO COMMAND
next slide thanks   -> heard "next slide zero next" / "next slide go next"                -> NO COMMAND
go to slide five please -> heard "go to slide five previous"                              -> NO COMMAND
```

**Root cause confirmed.** Re-running the identical audio with `"[unk]"` appended to the grammar (production engine otherwise untouched) turns the substitutions into deletions: `"go next slide"` → `"[unk] next slide"`, `"hundred next slide"` → `"[unk] next slide"`. Substitution, not deletion, is what corrupts the phrase.

**Filler reachability confirmed** (`filler.cpp`, public API only):

```
grammar vocabulary: 39 words
filler words the MATCHER supports : 25
of those, EMITTABLE by the decoder: 0
```

Their count of 24 is off by one (the union of `leadingFiller()` 17 + `trailingFiller()` 12 minus 4 overlaps is 25) — immaterial. The `matchCommand` tests do feed strings the shipped decoder cannot emit: `tests/test_command_matcher.cpp:154,155,162,163` use `"okay next slide"`, `"next slide please"`, `"go to slide five please"`, `"okay go to slide twelve"`.

**New, worse than reported.** The one phrasing that works in isolation also fails in real talk cadence. Narration + a natural pause + a bare command, fed as one continuous stream:

```
gap 200 ms : 0/4 -> "zero resume continue the slide resume continue the to slide zero next slide"
gap 400 ms : 0/4 -> "...to slide zero next slide"    (glued into ONE utterance)
gap 800 ms : 3/4  (endpoints separately)
gap 1500 ms: 4/4
```

Below ~800 ms of silence the decoder never endpoints, so the command is concatenated onto the preceding narration and cannot match. The presenter must stop talking for roughly a second before every command.

**Severity.** I disagree with SEV-1 on impact: the keyboard is untouched, and a bare, isolated `"next slide"` is 100% reliable across three voices and three rates, so the talk cannot be lost. I hold it at SEV-2 with SEV-1 *priority* — this is the marquee feature and it will look broken in the room.

**The tester's caveat about fixing this is verified and important.** With `[unk]` in the grammar, narration decodes as `"[unk] next slide"` (from *"which you can see on the next slide"*) and `"[unk] [unk] go to slide twelve [unk]"` (from *"if we could go to slide twelve please"*). Stripping `[unk]` as filler would convert both into live false triggers, the second one a slide jump. That trade must be made deliberately.

## (c) Teardown with voice armed — CONFIRMED (SEV-2); the "second deck" variant REFUTED

Ordering verified in source: `app_shell.hpp:88-90` declares `voice_`, `engine_`, `voiceGate_`, so reverse-order member destruction frees the decoder **before** the microphone is stopped, and `~AppShell` (`app_shell.cpp:54-56`) only calls `teardownWorkers()`. Decoding runs directly on miniaudio's real-time thread (`miniaudio_capture.cpp:110` → `voice_pipeline.cpp:61` → `app_shell.cpp:95`), not on a worker as the `voice_pipeline.hpp` header comment claims.

**Race window measured** on the production classes, silent room: 100 capture callbacks/s, **2.0% of wall time inside `VoskEngine::feed()`**, longest single call 12.4 ms.

**ASan, real unmodified `AppShell`: 1 SIGABRT in 70 destructions** (theirs: 1 in 35 — same order):

```
[0] voiceIsRunning=1 pipeline=0x60600090a4c0 windows=1
ERROR (VoskAPI:TraceBackBestPath ... likely bug in token-pruning algorithm)
ASSERTION_FAILED (VoskAPI:PruneForwardLinks():lattice-incremental-decoder.cc:259)
  Assertion failed: (frame_plus_one >= 0 && frame_plus_one < active_toks_.size())
exit rc=134
```

**TSan is decisive — three races, every one main-thread destruction vs. the audio thread:**

```
Write of size 8 by main thread:
  #0 unique_ptr<VoskEngine>::reset
  #3 pptv::AppShell::~AppShell()                     app_shell.cpp:56
Previous read of size 8 by thread T521:
  #1 pptv::AppShell::armVoice()::$_0::operator()     app_shell.cpp:95
  #8 pptv::VoicePipeline::onSamples                  voice_pipeline.cpp:61
  #16 MiniaudioCapture::onData                       miniaudio_capture.cpp:110

Write by main thread:  pptv::VoskEngine::stop()      vosk_engine.cpp:99
Previous read by T521: pptv::VoskEngine::feed()      vosk_engine.cpp:112
```

Reachable on **every** quit: `main.cpp:56` holds `AppShell` by value, and Cmd+Q routes through the quit filter so `exec()` returns normally and the destructor runs.

**Smallest fix** — mirror what `armVoice()`'s own failure path already does at `app_shell.cpp:108-110`; one line at the top of the destructor:

```cpp
AppShell::~AppShell() {
    voice_.reset();       // stops the mic and joins the audio thread FIRST
    teardownWorkers();
}
```

**Sub-claim REFUTED — "same bug, mid-talk: opening a second deck, 7/20 crashes."** The `engine_`-before-`voice_` reassignment hazard at `armVoice()` lines 80 vs 92 is real in source, but I measured **0/30** on the real `AppShell`, and more importantly the scenario is **unreachable**. `openDeck()` has exactly three call sites: `main.cpp:59` (once, at launch) and the two `StartView` signals (`app_shell.cpp:64-65`). While presenting:

```
pptv::PresentationWindow  visible=1 fullscreen=1 acceptDrops=0
pptv::StartView           visible=0 fullscreen=0 acceptDrops=1
```

The StartView is hidden (`app_shell.cpp:276`), `PresentationWindow` refuses drops, and there is no `QFileOpenEvent` handler, so Finder open-while-running is a no-op. The 7/20 figure came from their model harness, not the app.

## (d) Pause invisible, keyboard "P" dead — CONFIRMED, SEV-2

Reproduced end-to-end on the real app:

```
after VOICE 'pause presentation'        painted=86d1aeeb strip=""     <- no sign anything happened
after VOICE 'next slide' (paused)       painted=86d1aeeb strip=""     <- no sign why nothing moved
after keyboard P (documented un-pause)  painted=86d1aeeb strip=""
after VOICE 'next slide' post-P         painted=86d1aeeb strip=""
  >>> DID 'P' RESTORE VOICE? NO
after keyboard Right                    painted=10f42196 strip="Slide 2"   <- keyboard nav fine
after VOICE 'continue presentation'     painted=10f42196 strip="Resumed"
after VOICE 'next slide' post-continue  painted=c4d231e3 strip="Slide 3"
```

All three mechanisms verified: `RecognizerController::onPhrase` drops gated nav before the sink (`recognizer_controller.cpp:56-59`); `app_shell.cpp:90` and `:416` both hard-code `paused=false`; `notice.cpp:27-29` returns an empty string for `NoticeId::Paused` when false. `PresentationWindow::setPaused` (`presentation_window.hpp:41`) has zero callers, so `key_translator.cpp:137-143` can only ever emit `PausePresentation`. The `command_matcher.cpp:88-90` comment stakes BUG-17's residual risk on that parity, and it is not wired.

One correction to their framing: the *behaviour* is delivered — voice nav really is gated while paused, by `RecognizerController`. What is dead is the source-aware gate in `PresentationController` and every user-visible signal. Voice `"continue presentation"` is a working escape hatch (verified), but per finding (b) it is exactly the kind of phrase a presenter will say as *"okay, let's continue the presentation"* — which does not decode.

**Smallest fix:** in the `phraseHeard` lambda (`app_shell.cpp:100-104`), after `voiceGate_->onPhrase(phrase)`, mirror the owner's state and repaint:

```cpp
const bool paused = voiceGate_->state() == RecognizerController::State::Paused;
if (window_) { window_->setPaused(paused); }
lastNotice_ = noticeForRole(Notice{NoticeId::Paused}, NoticeRole::Audience, paused);
refresh();
```

## (e) Voice-failure reason never shown — CONFIRMED but SEV-3; the projector-modal half REFUTED

`voiceUnavailableReason_` written once (`app_shell.cpp:319`); its only reader is the inline getter at `app_shell.hpp:95`, which has **zero call sites** in `src/` or `tests/`. I also checked the generated meta-object: `build/pptv_ui_autogen/*/moc_app_shell.cpp` contains only `deckOpenAttempted` — the getter is not `Q_INVOKABLE`, not a slot, not a property, so there is no dynamic path either. The reachable real-world trigger is a denied microphone (`CaptureError::PermissionDenied`), and the presenter gets no message at all. SEV-3 rather than SEV-2: the keyboard is untouched, the harm is a confused presenter, not a lost talk.

**The `QMessageBox` half is refuted.** `app_shell.cpp:183/197` are indeed unparented and application-modal, but they fire only from `onDeckLoaded` failure, which requires an `openDeck()` — unreachable while presenting, per the reachability evidence in (c). They land on the start screen at launch, where a modal is correct. SEV-4.

## (f) The header's central safety claim is false as built — CONFIRMED, SEV-3

`vosk_recognizer.hpp:13-16` claims the decoder can produce *only* the five commands. My own decoder output, production grammar, unmodified:

```
"one seven fifteen hundred slide four presentation"
"continue go go to the previous slide four seven"
"zero resume continue the nine resume continue the to slide zero pause presentation"
"the resume to the next slide"
```

None of those is one of the nine grammar phrases. `vosk_recognizer_new_grm` with a word-list constrains the **vocabulary**, not the phrase set, and `grammarPhrases()` (`vosk_recognizer.cpp:54-58`) concatenates all 29 number words into a single pseudo-phrase, making any sequence of them legal. The property is delivered by `matchCommand`'s exact string equality — the second layer. Defensible engineering, mis-documented; and BUG-65's `unknownGrammarWords` guard (`vosk_engine.cpp:78-85`) protects the layer that does not do what the header says.

**Smallest fix:** none in code. Correct the comment to say the first layer constrains the vocabulary and the second layer (exact phrase equality in `matchCommand`) is what makes the phrase set closed.

## (g) Leaked window with a dangling controller — OVERSTATED, SEV-4

The leak is real and I reproduced both halves. Under TSan, `PresentationWindow`s accumulate across `AppShell` lifetimes (`windows=1`, `2`, `3` for three shells), and a key press to an orphan is a clean use-after-free:

```
[0] after ~AppShell: PresentationWindows=1 visible=1 fullscreen=1
[0] sending Right to the orphaned window...
ERROR: AddressSanitizer: heap-use-after-free
  #0 pptv::PresentationWindow::keyPressEvent(QKeyEvent*) presentation_window.cpp:50
freed by main; the AppShell that owned controller_
```

But the "still visible on the projector" and the key press are artifacts of my probe deleting the shell while the app runs. In shipped `main.cpp` the destructor runs *after* `exec()` returns, and Cmd+Q goes through `QuitFilter` → `QApplication::closeAllWindows()` (`quit_policy.cpp:37`), so the window is already closed and no event loop exists to deliver a key. `~QApplication` then reaps it. Real hygiene defect, no live exposure. **Fix:** parent `window_` and `start_` to the shell, or `delete window_; delete start_;` in `~AppShell`.

## (h) File picker in front of the room — REFUTED, SEV-4

Same reachability wall as (c). There is no user-accessible path to a second `openDeck()` while presenting: `StartView` is hidden, `PresentationWindow` has `acceptDrops=0`, no `QFileOpenEvent` handler exists, and macOS `applicationShouldHandleReopen` does not re-show hidden Qt windows. The tester's probe called `openDeck()` programmatically. What remains true — the error dialog is unparented and app-modal — is (e)'s SEV-4 note.

---

## Positive claims — spot-checked, all held

- **Voice can never quit.** `quitConfirmed_` has exactly one assignment (`presentation_controller.cpp:151`), reachable only from `confirmQuit()` (`app_shell.cpp:225`), triggered only by `UiRequest::ConfirmQuit`, produced only by `key_translator.cpp:43`. Voice produces `Command`, never `UiRequest`. Behaviourally: five voice commands behind the quit prompt moved nothing and dismissed nothing; window still open and visible.
- **No false triggers.** 15 narration/room sentences through the real decoder: **0/15** produced a command, including *"let me move to the next slide in our roadmap"*, *"and that brings us to the next slide"*, *"we have about fifteen minutes left for questions"*, *"revenue grew seventeen percent year over year"*.
- **Range checking.** `go to slide seventeen` and `go to slide zero` on a 10-slide deck: rejected, no movement, blackout not lifted.
- **Notices never expire** (BUG-29): `onTick` (`presentation_controller.cpp:155-172`) only times out the quit prompt; `NoticeClass::Transient` is set but nothing in `AppShell` acts on it — `lastNotice_` changes only on the next dispatch. Confirmed.

---

## Harnesses (all mine, all under `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/sk2/`)

- `probe_real.cpp` — real `AppShell` end-to-end; scenarios `blackout` / `pause` / `quit` / `ui` / `teardown`
- `decode.cpp` + `gen.sh` + `gen_narr.sh` + `cadence.py` — real Vosk on real TTS; `PPTV_GRAMMAR` overrides the grammar for the `[unk]` experiment. Results: `out_corpus.txt`, `out_unk.txt`
- `filler.cpp` — 39-word vocabulary vs 25 supported filler words, 0 emittable
- `teardown.cpp` → `teardown_asan` / `teardown_tsan` / `teardown_plain`; hit log `b_38.log`, race log `tsan_destruct.log`
- `audio_live.cpp` — measures the real capture rate and the width of the teardown race window
- `cflags.sh` / `cflags_asan.sh` / `cflags_tsan.sh` — link against the existing `build`, `build-asan`, `build-tsan` trees (both sanitizer trees rebuilt clean before use)

---

**TL;DR in plain English**

I re-ran the other tester's eight claims myself rather than taking their word for it, and drove the actual application rather than a stand-in.

Five hold up. The privacy blackout really is broken — I hid the deck, said "next slide", and the confidential deck came straight back on screen and moved forward. Their suggested one-line fix isn't quite enough: it stops the deck reappearing but the slide still creeps forward invisibly, so I've written the fix that actually closes it. Natural speech really is ignored about 87% of the time, which I confirmed with completely different voices and my own phrase list — and I found it's worse than they said: even the one phrasing that does work stops working if you say it in the normal flow of talking. You have to go silent for about a second first. Quitting with the microphone on really can crash the app; I caught it crashing once in seventy tries, and a specialist tool proved the underlying flaw outright. Pausing really does give you no on-screen sign, and the "press P" way out really doesn't work. And the file that claims the speech engine "can only ever say the five commands" is simply not true as built — though a second safety net downstream is quietly doing the job anyway.

Three claims I've knocked down or shrunk. Two of the scarier ones — a crash when opening a second deck mid-talk, and an error dialog appearing in front of the audience — can't actually happen, because once you're presenting there's no button, menu, or drag-and-drop that lets you open another file. A third, about a leftover window, is a genuine tidiness bug but harmless in the shipped app because it only happens as the program is already shutting down.

The good news the other tester reported all survived my checking: no spoken word can quit the app or end the talk, ordinary presenter chatter never moves the deck by accident, out-of-range slide numbers are refused rather than guessed, and the keyboard always works.