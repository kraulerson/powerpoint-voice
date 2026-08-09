UAT-5 — LIFECYCLE AND CONCURRENCY OF THE VOICE PATH. 11 harnesses built, all reproductions below re-run at least twice. Scratch: `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/` (`build.sh <src.cpp> <asan|tsan|plain>` builds any of them against the project's own `build*/libpptv_core.a`). Baseline confirmed first: `ctest` in `build` = 253/253 passed.

---

## VP-1 — SEV-1, VERIFIED. AppShell frees the live VoskEngine out from under the audio thread on every quit. Crash in front of the room.

`/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice/src/ui/app_shell.hpp:88-90`

```cpp
std::unique_ptr<VoicePipeline> voice_;
std::unique_ptr<VoskEngine> engine_;
std::unique_ptr<RecognizerController> voiceGate_;
```

Members are destroyed in reverse declaration order, so `~AppShell` destroys **`engine_` before `voice_`**. Nothing ever calls `voice_->stop()` — `grep -n "voice_" src/ui/app_shell.cpp` shows the only writes are in `armVoice()`. So the microphone is still open and `MiniaudioCapture::onData` is still running `VoicePipeline::onSamples` → `decode_` → the `armVoice` lambda at `app_shell.cpp:94-96`, which is `engine_ ? engine_->feed(s, n) : QString()`, against a VoskEngine that has already been freed. `~unique_ptr` does not null its stored pointer, so the `engine_ ?` guard is truthful-looking and useless.

**Exposure window measured** (`r10_window.cpp`): `~VoskEngine` (`vosk_recognizer_free` + `vosk_model_free`) takes **7.24 / 7.26 / 7.43 / 10.49 / 19.58 ms**. A CoreAudio capture callback on a MacBook Pro is ~10.7 ms (512 frames @48 kHz). One to four callbacks land inside the window *every time*.

**Reproduction** — `r1_shutdown_order.cpp`: the three real production objects in the shipped declaration order, only the capture device replaced (no mic on this machine). The fake device is faithful — its `stop()` **joins** its IO thread exactly as `ma_device_stop()` drains CoreAudio's, so it cannot manufacture the bug. `r1_control_order.cpp` is byte-identical except the three declarations are reordered.

```
Release build, 5 arm/teardown cycles per run:
  shipped order : 0 139 0 139 0 139   (139 = SIGSEGV)      + earlier 9/10 runs crashed
  control order : 0 0 0 0 0 0                              + earlier 0/10 runs crashed
```

ASan stack (first iteration, `r1_shutdown_order-asan`):
```
SEGV on unknown address 0x21a3 ... READ
 #0 kaldi::LatticeIncrementalDecoderTpl<...>::PruneForwardLinksFinal()
 #2 Recognizer::Result()
 #3 pptv::VoskEngine::feed(short const*, unsigned long)  vosk_engine.cpp:123
 #4 pptv::VoicePipeline::onSamples(short const*, unsigned long)  voice_pipeline.cpp:61
 #5 <FakeMic IO thread>
```

Reachability on the presenter's machine: voice must have armed (real microphone, permission granted — hence this is invisible on the dev Mac mini), then any quit route reaches `~AppShell` — the two-step Esc chord (`window_->close()` → last visible window → `QApplication` quits → `exec()` returns → `main.cpp:56` `shell` destructs), Cmd+Q / Dock Quit via the BUG-31 quit filter, or logout (BUG-44). Note the interaction with BUG-42: `teardownWorkers()` runs first and can wait 5 s, so the crash lands at the *end* of an already-slow quit — i.e. it reads as "the app crashed on exit" and macOS puts a crash reporter on the projector.

Same class, same function: `voiceGate_` is also destroyed before `voice_`, and `app_shell.cpp:100-104` reads it from the `phraseHeard` slot. Not reachable at shutdown only because the event loop has already stopped.

The fix is one line of reordering (declare `voice_` last), which the control harness proves sufficient. Explicitly calling `voice_->stop()` at the top of `~AppShell` would be belt-and-braces.

---

## VP-2 — SEV-1, VERIFIED. `VoskEngine::start()` SEGVs instead of returning an error when a model file is present but unreadable — and `armVoice()` runs during the talk.

`vosk_engine.cpp:86`. The header says "Voice stays OFF on any error"; `app_shell.cpp:313-320` says "A failure is reported and ignored: the keyboard drives everything regardless". Both assume `start()` returns.

`r3_model_vanishes.cpp` copies the vendored model, runs `prepareRecognizer()` (which passes), then mutates the copy, then calls `start()`:

| mutation | result |
|---|---|
| whole dir removed | returns `ModelMissing` — safe |
| `am/final.mdl` removed | returns `ModelMissing` — safe |
| `conf/mfcc.conf` removed | returns `ModelMissing` — safe |
| `ivector/` removed | returns `EngineUnavailable` — safe |
| **`graph/HCLr.fst` overwritten with garbage** | **process exits 139 = SIGSEGV** |

```
SEGV on unknown address 0x000000000000 ... READ
 #0 kaldi::LatticeIncrementalDecoderTpl<...>::InitDecoding()
 #2 Recognizer::Recognizer(Model*, float, char const*)
 #3 vosk_recognizer_new_grm
 #4 pptv::VoskEngine::start(pptv::RecognizerSetup const&)   vosk_engine.cpp:86
```
Vosk logs `Runtime graphs are not supported by this model` and then hands a null FST to the decoder. `modelIsGrammarCapable()` (`vosk_recognizer.cpp:93-100`) only checks that `HCLr.fst` and `Gr.fst` **exist**, never that they are readable, so the guard passes.

This is reachable, not theoretical: the model is a plain directory tree in the build dir (`PPTV_VOSK_MODEL_DIR` is the absolute path `…/build/vosk/model/vosk-model-small-en-us-0.15`, stored UTF-16 in the binary), and **the build will not repair it**. `build/build.ninja:684` — `build vosk/model.stamp: CUSTOM_COMMAND …/vosk-model-small-en-us-0.15.zip` — the stamp depends only on the zip, so once `model.stamp` exists no amount of `cmake --build` re-extracts a damaged or partially-written model. An interrupted extract, a full disk, a bad `rsync`/Time Machine restore, or a cloud-sync placeholder produces exactly "file exists, content wrong". And `armVoice()` is invoked from the queued lambda at `app_shell.cpp:307-322`, i.e. *after* the fullscreen window is up — so the SIGSEGV happens seconds into the talk, on the projector.

Minimum fix: wrap `vosk_recognizer_new_grm`/`vosk_model_new` so a failure cannot take the process with it, and/or make `modelIsGrammarCapable()` validate the FST headers rather than just `QFileInfo::exists`.

---

## VP-3 — SEV-1, VERIFIED. The stated core safety property is false: the decoder is *not* incapable of emitting non-command text. Only `matchCommand()` stands between the audience and the deck.

`vosk_recognizer.hpp:13-16`: *"THE SAFETY PROPERTY THIS FILE EXISTS FOR: the decoder must be able to produce ONLY the five commands. Not 'usually produce', not 'produce them best' — only."*

Vosk grammars constrain the **vocabulary**, not the phrase structure — `vosk_recognizer.cpp:34` acknowledges this ("Vosk grammars are word lists, not patterns") without following it through. Because `grammarJson()` emits no out-of-grammar token, the decoder has **no legal way to say "that wasn't one of my phrases"** and must map every utterance onto grammar words.

Driven through the real `VoicePipeline` + real `VoskEngine` + real `RecognizerController` with synthesised speech (`r8_decode_probe.cpp` / `r9_safety_probe.cpp`; audio from macOS `say`, four voices, 16 kHz mono, 1 s silence between utterances):

- **30 ordinary presenter sentences containing no command → 30/30 produced a grammar phrase.** Examples: `"previous slide hundred seventy to"`, `"the next slide continue zero go to slide hundred"`, `"the slide go to zero continue"`.
- **40 short audience interjections → 40/40 produced a grammar phrase.** `"Good point."` decoded as **`"go to slide"`**. `"Next."` → `"next"`. `"Previous."` → `"previous"`. `"Perfect."` → `"four fifty"`.
- **10 narration sentences that legitimately mention slides → 10/10** produced phrases containing exact commands as substrings.

Dispatches in all 80: **0** — every one was rejected by `matchCommand()`'s phrase-level rule. That is the entire defence, and it is a string comparison against a decoder that is provably willing to emit `"go to slide"` when someone says "Good point." The property the file claims to enforce is not enforced; what is actually enforced is "the matcher usually rejects the salad".

**Positive control** (`commands_spaced.raw`, five real spoken commands with 1 s gaps): 5/5 decoded and dispatched correctly — `NextSlide`, `PreviousSlide`, `GoToSlide(17)`, `PausePresentation`, `ContinuePresentation`. The voice path *works*; this is the first time it has decoded human-like speech end-to-end (relevant to BUG-64(b)).

**Fix verified in the same harness.** Appending `"[unk]"` to the grammar array (`r9_safety_probe-plain <raw> unk`):

| corpus | shipped grammar | with `[unk]` |
|---|---|---|
| 30 non-command sentences | 30 utterances of grammar salad | 29, now dominated by `[unk]` tokens |
| 40 short interjections | 40 utterances | **23** — 17 now produce nothing at all |
| 5 real commands | 5 dispatched | **5 dispatched** (no regression) |

Strictly better on this corpus at no recognition cost. Also worth noting: with continuous speech and no pause, Vosk never endpoints — 5.5 s of five spoken commands back-to-back came out as **one** utterance (`"next slide previous slide go to slide seventeen pause the presentation continue the presentation"`), matched nothing, dispatched nothing. A presenter who does not pause gets no voice control at all.

---

## VP-4 — SEV-2, VERIFIED (latent, not reachable through today's UI). Arming voice twice is the same use-after-free with a ~1 s window.

`app_shell.cpp:80` — `engine_ = std::make_unique<VoskEngine>();` — replaces the engine while the *previous* `voice_` is still capturing and still calling `engine_->feed()`. The old engine is freed at the assignment and the new one then spends the whole model load (~1 s) being built, so the audio thread hammers freed memory for that entire time. It is also an unsynchronised read/write of the `engine_` `unique_ptr` itself (GUI thread writes, audio thread reads at `app_shell.cpp:95`).

`r2_rearm.cpp` (shipped member order, `armVoice()` called twice):
```
armed once; audio flowing
arming a SECOND time ...
ASSERTION_FAILED (VoskAPI:CuSubMatrix():cudamatrix/cu-matrix-inl.h:41) Assertion failed: ...
EXIT=134   (SIGABRT)
```
I could not find a second-`openDeck` path in the shipped UI: `openDeck` has three callers (`main.cpp:59`, `StartView::browseRequested`, `StartView::fileDropped`), `start_` is `hide()`den on success, its Cmd+O `QShortcut` is `Qt::WindowShortcut` so it cannot fire from a hidden window, and `UiRequest` (`key_translator.hpp:22-30`) has no open-deck action. So this is latent today — but `armVoice()` is public API with an inviting doc comment, and it becomes live the moment anything adds a "close deck" / "open another" route.

---

## VP-5 — SEV-2. `modelIsGrammarCapable()` does not check the condition it was written to check.

The comment at `vosk_recognizer.cpp:94-96` names the exact hazard: a model with `graph/HCLG.fst` "silently ignores the grammar and decodes the FULL vocabulary — no error, no warning, and an audience that can drive the slides." The implemented test is `exists(HCLr.fst) && exists(Gr.fst)` — it never checks that `HCLG.fst` is **absent**, and Vosk's `Model` prefers `HCLG.fst` when present.

`r6_grammar_guard.cpp` copies the model and drops a valid Kaldi FST in as `graph/HCLG.fst`:
```
prepareRecognizer -> error=0  (0 == 'safe to arm voice')       <- VERIFIED: the guard does not fire
VoskEngine::start -> err=1                                      <- saved by an unrelated missing graph/words.txt
```
**VERIFIED**: the guard passes a model it was specifically written to reject. **HYPOTHESIS** (not reproducible offline — needs a real static-graph model such as `vosk-model-en-us-0.22`, which ships `graph/words.txt`): with a genuine HCLG model the run would proceed and decode the full ~200k-word vocabulary. Only Vosk's accidental missing-file error stopped it here. One-line fix: `&& !QFileInfo::exists(graph.filePath("HCLG.fst"))`.

---

## VP-6 — SEV-2, VERIFIED. The real-time audio callback contract is violated by the only sink that implements it, and it overruns.

`audio_capture.hpp:37-40`: *"Audio arrives on a REAL-TIME thread. The contract for anything called there: no allocation, no locks, no Qt objects, no logging. The sink below is invoked on that thread and must obey it."*
`voice_pipeline.hpp:22-24`: *"Nothing Qt-related happens there — samples are converted and decoded on a dedicated worker."*

**There is no worker.** `VoicePipeline::onSamples` runs inline on miniaudio's callback thread (`miniaudio_capture.cpp:110` calls `sink_` directly, holding `sinkMutex_`) and in one call allocates two `std::vector`s, runs the Vosk nnet3 decode, builds a `QString`, and posts a Qt event (`voice_pipeline.cpp:67`, which allocates a `QMetaCallEvent` and takes Qt's global post-list lock).

`r7_rt_budget.cpp` times the sink with real speech through the real decoder:
```
549 callbacks, 160-sample (10 ms) period — the standard CoreAudio budget
  median=0.015  p95=0.042  p99=0.943  MAX=22.847 ms
  OVER BUDGET: 1 of 549
```
The 22.8 ms outlier is the final-result path (`vosk_recognizer_result` → `FinalizeDecoding`), i.e. it fires exactly when a command completes — 2.1× the 10.67 ms CoreAudio period, so CoreAudio drops the buffer(s) immediately following every recognised command. Practical effect: two commands in quick succession, the second loses its onset. The doc comment should either be corrected or the decode moved off the callback thread as it claims.

---

## VP-7 — SEV-3, VERIFIED. `VoicePipeline::setDecoder()` races `onSamples()`, including a vptr race.

`voice_pipeline.cpp:21` writes `decode_` with no synchronisation; `voice_pipeline.cpp:61` reads and calls it on the audio thread. `r5_races.cpp decoder` under TSan:
```
WARNING: ThreadSanitizer: data race
  write: std::function<...>::operator=  -> pptv::VoicePipeline::setDecoder  voice_pipeline.cpp:21
  read : std::function<...>::operator bool -> pptv::VoicePipeline::onSamples  voice_pipeline.cpp:61
WARNING: ThreadSanitizer: data race on vptr (ctor/dtor vs virtual call)
  __func::__clone  <- setDecoder   vs   __value_func::operator()  <- onSamples voice_pipeline.cpp:61
```
The vptr race is the destructive one — a virtual call into a `__func` being replaced. Not reachable in the shipped wiring (`armVoice` calls `setDecoder` before `start()`), but the class is documented as injectable and its header states a THREADING contract that is silent on this. `setCapture`, repeated `start()`, and destroy-during-decode were all clean under TSan (`r5_races.cpp capture|restart|destroy`) — because `setCapture` and `~VoicePipeline` both go through `stop()`, and the fake device drains like `ma_device_stop` does.

---

## VP-8 — SEV-4, HYPOTHESIS (40 ASan runs did not reproduce). `IAudioCapture::stop()` has no drain contract, but `VoicePipeline::stop()`'s correctness depends on one.

`audio_capture.hpp:47` is a bare `virtual void stop() = 0;`. `voice_pipeline.cpp:36-40` asserts on the strength of calling it that "no callback is in flight against a pipeline that is going away" — true only because `MiniaudioCapture::stop()` happens to call `ma_device_stop()`. `voice_pipeline.hpp:55-57` also destroys `decode_` **before** `capture_`, so a non-draining backend would free the decoder while it runs. `r11_nondraining.cpp` implements a backend that is legal against the published interface and does not join; 40 ASan runs produced no report, so I am reporting this as a documentation/contract defect only, not a demonstrated crash. The one-line fix is to state the drain requirement on the interface.

---

## VP-9 — SEV-4, VERIFIED. `vosk_set_log_level(-1)` does not do what `vosk_engine.cpp:56-59` claims.

The comment says Vosk's stderr output "is a disclosure channel: it would print heard words. Silence it before anything is loaded." It sets the Kaldi *verbose* level, which suppresses `KALDI_VLOG` only — `KALDI_WARN` and `KALDI_ERR` still reach stderr after the constructor has run. Observed in every harness, e.g. `r3_model_vanishes` (constructor runs before `start()`):
```
ERROR (VoskAPI:Model():model.cc:122) Folder '/…/modelcopy-gone' does not contain model files…
WARNING (VoskAPI:PruneForwardLinksFinal():lattice-incremental-decoder.cc:391) Negative extra_cost: -inf
```
These particular messages carry filesystem paths and decoder internals rather than recognised words, so the disclosure risk is low — but the mitigation as written is ineffective and should not be relied on.

---

## Negative results (no defect found)

- **No leaks.** `r4_leak.cpp` (corrected member order, real model, real pipeline) — `leaks --atExit` over 4 full arm/disarm cycles: **`0 leaks for 0 total leaked bytes`**. Peak RSS over 15 cycles: 144.5 → 175.9 MiB, deltas decaying to +0.016 MiB by cycle 12 — allocator high-water, not per-cycle growth.
- `setCapture()` while audio flows, `start()` called repeatedly while audio flows, and destroying the pipeline mid-decode are all clean under TSan and ASan **given a draining capture backend** (`r5_races.cpp`).
- `VoskEngine::feed()` from two threads is not reachable in the shipped wiring — there is exactly one audio thread. The reachable version of that hazard is VP-1 (GUI-thread free vs audio-thread `feed`).

## Context, outside the lens but load-bearing for VP-2

`build/powerpoint_voice.app` contains only `Info.plist` and the executable. `otool -l` shows `LC_RPATH` = `/Users/karl/Documents/…/build/vosk`, `otool -L` shows `@rpath/libvosk.dylib` plus `/opt/homebrew/opt/{qtbase,libzip,pugixml}`, and the model path baked into the binary is `…/build/vosk/model/vosk-model-small-en-us-0.15`. The bundle is not relocatable and the model is a mutable directory in the build tree, which is what makes VP-2's "present but unreadable" state reachable.

## Ranked

1. **VP-1** SEV-1 — crash on quit whenever voice armed. 12/16 Release runs, 0/16 with the order corrected. One-line fix.
2. **VP-2** SEV-1 — SIGSEGV inside `armVoice()` mid-talk on a damaged model file; the build never repairs the model.
3. **VP-3** SEV-1 — the decoder's stated safety property does not hold; `[unk]` fix verified with no regression.
4. **VP-5** SEV-2 — the TM-002 guard passes the exact model shape it was written to reject.
5. **VP-6** SEV-2 — RT-thread contract violated and measurably overrun; the header's "dedicated worker" is not true.
6. **VP-4** SEV-2 — latent double-arm UAF (SIGABRT), unreachable today.
7. **VP-7** SEV-3, **VP-8** SEV-4, **VP-9** SEV-4.

**TL;DR (plain English):** The voice feature works — I fed it computer-generated speech and all five spoken commands moved the deck correctly. But I found three serious problems. First, whenever you quit the app with voice switched on, it shuts the speech engine down while the microphone is still feeding it, and the app crashes on the way out — it did this in 12 of 16 test runs, and a one-line reordering fixed it in all 16. Second, if any one file in the speech model gets damaged, the app doesn't politely turn voice off as it's supposed to — it crashes outright, and it does this a few seconds into the presentation; worse, rebuilding the app won't repair that file. Third, the promise that the microphone "can only ever hear the five commands" isn't true: it can only use those *words*, but it strings them together freely — someone saying "Good point" came out as "go to slide". Nothing bad actually got triggered in 80 tries, because a later safety check caught every one, but the protection is weaker than the code claims. I tested a one-word fix for that and it made things clearly better without breaking the real commands. I also checked for memory leaks and found none.