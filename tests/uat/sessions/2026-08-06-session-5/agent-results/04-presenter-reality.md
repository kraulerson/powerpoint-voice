## UAT-5 — Voice in front of an audience

Baseline re-verified at HEAD 079de1a: 230 + 18 = 248 tests green before I started. Everything below is reproducible with the harnesses listed at the end.

Two things made this session different from a code read: macOS `say` emits **16 kHz mono s16** — the recognizer's exact native format — so I drove the **real Vosk decoder with real synthesised speech**; and `armVoice()` **succeeds on this machine** (CoreAudio hands out a default input device even with no mic), so the teardown findings are against the real `AppShell`, not a model.

---

## SEV-1 (a) — A voice command defeats the privacy blackout. VERIFIED.

`presentation_controller.cpp:117-120` un-holds on any accepted navigation and is **source-blind**. Esc does not pause the recognizer, so the mic is live and unpaused during the blackout.

```
after Esc (blackout)      : [mode=Holding(BLACKOUT) slide=2 strip=""]
after voice "next slide"  : [mode=Presenting slide=3 strip="Slide 3"]
>> did a VOICE command un-blank the projector? YES - deck is back on the wall

blackout + "go to slide 7": [mode=Presenting slide=7 strip="Slide 7"]   *** revealed + jumped
```

Karl hits Esc because someone walks in. Anyone in earshot says "next slide" and the Confidential deck is back on the wall **and advanced**. This is TM-002/012/019 verbatim.

**This was already found and deferred.** `tests/uat/sessions/2026-08-05-session-3/agent-results/04-regression-guard.md:166` reports it as SEV-3, and the deferral rests on one stated fact: *"Neither is live today: ... RecognizerController is never instantiated by AppShell."* F8d instantiated it (`app_shell.cpp:89`) and `CommandSource::Voice` now has a real call site (`app_shell.cpp:90`). The precondition for the deferral is dead and nothing re-triaged it. Session 3 even wrote the one-line fix (`src == CommandSource::Keyboard` in the un-hold condition); it was not applied.

Related, still true: `undoJump()` has no `CommandSource`/`paused` parameter and no product caller — a latent second index-computing entry point.

## SEV-1 (b) — 87% of natural spoken commands do not work. VERIFIED.

34 of 39 trials across 3 voices / 2 accents / 3 speech rates produced **no command**:

```
"okay next slide"               -> heard "go next slide"              -> NO COMMAND
"alright next slide"            -> heard "hundred next slide"         -> NO COMMAND
"so next slide"                 -> heard "zero next slide"            -> NO COMMAND
"next slide please"             -> heard "next slide previous"        -> NO COMMAND
"next slide thanks"             -> heard "next slide zero next"       -> NO COMMAND
"okay previous slide"           -> heard "go previous slide"          -> NO COMMAND
"previous slide please"         -> heard "previous slide previous"    -> NO COMMAND
"okay pause presentation"       -> heard "go pause presentation"      -> NO COMMAND
"pause the presentation please" -> heard "pause the presentation previous" -> NO COMMAND
"go to slide five please"       -> heard "go to slide five previous"  -> NO COMMAND
"next slide"                    -> heard "next slide"                 -> NEXT   (bare only)
```

**Root cause, isolated:** the grammar has no `[unk]` sink, so Vosk force-aligns every out-of-vocabulary word onto an in-vocabulary one. Re-running the identical corpus with `[unk]` appended to the grammar turns `"go next slide"` into `"[unk] next slide"` — proving substitution, not deletion, is what corrupts the phrase.

**The consequence is that the entire UAT-2 BUG-11/12 fix is structurally unreachable.** `filler_check` (public API only) shows the grammar vocabulary is 39 words and:

```
filler words the MATCHER supports : 24
of those, EMITTABLE by the decoder: 0
```

All 24 words in `leadingFiller()`/`trailingFiller()` (`command_matcher.cpp:26-43`) are outside the grammar. The decoder can never produce one, so the strip loop can never remove one. The unit tests for that code pass by feeding the matcher strings the shipped decoder cannot emit — that is where UAT-4's zero mutation kills came from on this path.

Note the trap for the presenter: `"um next slide"` **works** (a filled pause is absorbed as silence) while `"okay next slide"` does not. There is no learnable rule.

## SEV-2 (c) — Quitting with voice armed corrupts memory. VERIFIED under ASan and TSan.

`app_shell.hpp:88-90` declares `voice_` **before** `engine_`. Members destroy in reverse order, so `~AppShell` frees the decoder while `voice_` still owns the open microphone — and `~AppShell`'s body (`teardownWorkers()`) touches only the deck-load and pre-render workers. The audio thread is meanwhile inside the decoder lambda at `app_shell.cpp:94`.

Real, unmodified `AppShell` (`probe2_asan teardown`):
```
voiceIsRunning=1 — destroying
ERROR: AddressSanitizer: attempting double-free on 0x6030007bf790 in thread T520
ASSERTION_FAILED (VoskAPI:ClearActiveTokens ...) Assertion failed: (num_toks_ == 0)
```
1 in 35 real runs here, where this machine's input device delivers near-silence so `feed()` returns early most of the time. With an audio thread actually decoding — a real mic array in a room full of speech — an ownership-faithful model crashes **38/40**:
```
#0 Recognizer::Result()+0x10 (libvosk.dylib)
#1 pptv::VoskEngine::feed(short const*, unsigned long)   vosk_engine.cpp:123
#2 pptv::VoicePipeline::onSamples(short const*, unsigned long) voice_pipeline.cpp:61
```
TSan names it directly: write at destruction (main thread) vs read of `engine_` from the audio thread inside `voice_pipeline.cpp:61`. `VoskEngine` has no synchronisation on `d_->rec`, and `stop()`/`feed()` race.

`main.cpp:56` holds `AppShell` by value, so `~AppShell` runs on **every quit**. This compounds BUG-42 (5 s to quit) and BUG-44 (logout quits): logout can now crash on the way out.

`armVoice()`'s own failure path gets the order right (`voice_.reset(); engine_.reset();` — `app_shell.cpp:108-110`), which is what makes the destructor order an oversight rather than a decision.

**Same bug, mid-talk:** opening a **second deck** re-runs `armVoice()`, which reassigns `engine_` (line 80) *before* replacing the pipeline (line 92), leaving the first microphone feeding a freed engine — **7/20 crashes**.

## SEV-2 (d) — Pause is invisible, and the documented keyboard escape hatch does not exist. VERIFIED.

```
after "pause presentation" : strip=""   gate=Paused     <- no sign anything happened
after "next slide" PAUSED  : strip=""   sinkCalls +0    <- no sign why nothing moved
after keyboard "P"         : gate=Paused                <- STILL PAUSED
after "next slide" post-P  : slide unchanged            <- voice still dead
arrow Right                : slide advances             <- keyboard nav does still work
```

Three independent reasons the presenter can never see "Paused":
1. `RecognizerController` drops gated nav **before** the sink, so `applyResult` never runs.
2. `app_shell.cpp:90` hard-codes `paused=false` into `dispatch()`, so `Notice{NoticeId::Paused}` is never produced.
3. `app_shell.cpp:416` hard-codes `paused=false` into `noticeForRole()`, which returns an **empty string** for `NoticeId::Paused` when false (`notice.cpp:27-29`).

The whole source-aware pause gate at `presentation_controller.cpp:46` is dead code in the shipped app — which also means a mutation removing the `RecognizerController` gate would be caught by nothing.

And `PresentationWindow::setPaused()` has **zero callers**, so `ctx.paused` is permanently false and `Key_P` can only ever emit `PausePresentation` (`key_translator.cpp:140`). The `command_matcher.cpp:88-90` comment stakes BUG-17's residual risk on exactly this: *"The residual 'stuck in Paused' risk this re-opens is covered by keyboard parity (F6)."* It is not wired.

## SEV-2 (e) — The presenter is never told voice failed to arm. VERIFIED.

`voiceUnavailableReason_` is written at `app_shell.cpp:319`. Its only reader is the inline getter at `app_shell.hpp:95`, and that getter has **zero call sites** in `src/` or `tests/`. It is not a slot, not `Q_INVOKABLE`, not a property — there is no dynamic path either.

Answers to the brief: the presenter is **not** told; they **can** still present (keyboard is untouched); the message appears **nowhere**, so it cannot land on the projector. On this machine the reason is empty only because arming succeeded — the string is unreachable regardless.

The dialog that *can* land on the projector is a different one: `QMessageBox::warning(nullptr, ...)` at `app_shell.cpp:183/197` is **unparented and modal**. Opened while presenting it blocks the whole app; verified `parent=NULL` with the presentation window fullscreen. Where an unparented modal lands on a two-screen macOS setup is HYPOTHESIS (offscreen QPA can't place it), but Qt centres it on the active window's screen, which during a talk is the projector.

## SEV-3 (f) — The file's central safety claim is false as built. VERIFIED.

`vosk_recognizer.hpp:12-16`: *"the decoder must be able to produce ONLY the five commands. Not 'usually produce', not 'produce them best' — only."*

Observed outputs from the real decoder:
```
"resume the previous slide the nine zero"
"sixty resume continue go to slide four"
"nine ten eleven the next slide"
"one seven fifteen hundred slide"
```
`vosk_recognizer_new_grm` with a word-list grammar constrains the **vocabulary**, not the phrase set — and `grammarPhrases()` pushes all 29 number words in as one concatenated pseudo-phrase (`vosk_recognizer.cpp:53-57`), which makes any sequence of them legal. The safety property is in fact delivered by `matchCommand`'s exact string equality — the second layer. That is defensible engineering, but the threat-model justification and BUG-65's guard rest on the first layer, and the first layer does not do what it says.

## SEV-3 (g) — `~AppShell` leaks the fullscreen window with a dangling controller. VERIFIED under ASan.

`window_` and `start_` are raw, unparented `new`s that nothing deletes.
```
destroying the AppShell...
PresentationWindows after  : 1
still VISIBLE on projector : 1   still FULLSCREEN : 1
>> sending it a key press...
ERROR: AddressSanitizer: stack-use-after-scope
  #0 pptv::PresentationWindow::keyPressEvent(QKeyEvent*) presentation_window.cpp:50
```
Line 50 is `controller_ ? controller_->mode() : ...` reading a destroyed `AppShell`'s member. In shipped `main.cpp` the exposure is between `~AppShell` and `~QApplication`.

## SEV-3 (h) — A mistyped file mid-talk puts the file picker in front of the room. VERIFIED.
```
=== broken deck opened while deck 1 is on the projector ===
  >>> MODAL BLOCKING THE APP: QMessageBox parent=NULL text="That file is not a PowerPoint (.pptx) file."
  VISIBLE pptv::StartView          fullscreen=0 parent=NULL
  VISIBLE pptv::PresentationWindow fullscreen=1 parent=NULL   <- deck still on the wall behind it
```

---

## What actually holds up

- **Voice can never end the talk or quit the app.** 1,331 three-phrase sequences × 4 modes (5,324 runs): `quitConfirmed` never true, window never closed. Structurally airtight too — `quitConfirmed_` has exactly one writer, reachable only from `UiRequest::ConfirmQuit`, which only `KeyCommandTranslator` produces, and voice never produces a `UiRequest`.
- **The quit prompt swallows voice completely.** Five commands behind it: no movement, no dismissal. (Minor: "pause presentation" still flips the recognizer gate behind the overlay, so voice can come back dead after cancelling.)
- **Range checking is correct and never clamps.** `go to slide seventeen`, `zero`, and `one hundred` on a 10-slide deck all rejected at slide 1 with "Deck has 10 slides".
- **Burst handling is clean.** Two phrases in milliseconds → two advances. 50 in a burst → 50 dispatches, no drops, no reentrancy, stops at the last slide. 50 alternating pause/continue → consistent gate state.
- **Mid-pre-render commands work** — jump accepted, surface shows "Rendering slide 9...".
- **No false trigger from 20 narration/room sentences**, including "let me move to the next slide in our roadmap", "and that brings us to the next slide", "we have about fifteen minutes left", "revenue grew seventeen percent". The garbage-prefix behaviour from (f) is accidentally protective here — but note it is the *same mechanism* that breaks (b), so fixing (b) by stripping `[unk]` as filler would immediately turn "which you can see on the next slide" (`[unk] next slide`) into a live false trigger. That trade needs deciding deliberately.

One interaction with known-deferred BUG-29: notices never expire, so a mis-heard `go to slide N` leaves "Deck has 10 slides" on the projector until the next accepted command.

---

## Harnesses (all under `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/`)

- `decoder_probe.cpp` + `corpus.sh` + `corpus2.sh` — real Vosk decoder driven by real TTS speech; `PPTV_GRAMMAR` env var overrides the grammar for the `[unk]` experiment. Results in `out-samantha.txt`, `out2-samantha.txt`.
- `filler_check.cpp` — proves the matcher's filler support is unreachable through the grammar.
- `state_harness.cpp` — AppShell's wiring transcribed verbatim with a fake decoder; scenarios S1-S8. Results in `out-state.txt`.
- `teardown_probe.cpp` — ownership-order reproduction (`destruct` / `rearm`), built as `teardown_asan` and `teardown_tsan`.
- `appshell_probe.cpp`, `appshell_probe2.cpp`, `appshell_probe3.cpp` — the real `AppShell` (`probe2_asan leak|teardown`, `probe3_asan`).

No project file was created, edited, or deleted; the confidential deck was never opened. Sanitizer builds reused the existing `build-asan`/`build-tsan` trees (`ninja: no work to do`).

---

**TL;DR in plain English**

I got the app to listen to real synthesised speech, which nobody had done before. The headline: when you speak to it the way a human actually speaks — "Okay, next slide", "next slide please" — it ignores you about 87% of the time. Only a bare, isolated "next slide" reliably works. The reason is that the speech engine is locked to a 39-word list, and any word outside that list gets silently swapped for one that *is* in the list, which garbles the command. All 24 of the polite filler words the app was specifically fixed to handle are outside that list, so that fix cannot ever run.

Second: the privacy blackout is broken. Karl presses Esc to hide the deck, and anyone in the room saying "next slide" puts it straight back on the wall and moves it forward. This exact problem was found in an earlier test round and filed away as harmless *because voice wasn't switched on yet*. Voice is switched on now, and nobody went back to check.

Third: quitting the app while the microphone is on can crash it, because the app throws away the speech engine while the microphone is still talking to it. I proved this on the real app with memory-checking tools.

Also: pressing "P" to un-pause does nothing at all, there is no on-screen sign that pausing worked, and if voice fails to start the app writes down the reason and then never shows it to anyone.

The good news is genuinely good: no spoken command can ever quit the app or end the presentation — I tried over five thousand combinations — the keyboard always works as a fallback, out-of-range slide numbers are correctly refused instead of guessed at, fifty commands at once causes no trouble, and none of twenty ordinary sentences of presenter or audience chatter moved the deck by accident.