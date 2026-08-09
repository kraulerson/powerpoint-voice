Baseline re-verified myself: `ctest` in `/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice/build` = **253/253 passed**. All file:line citations in the report were checked against the source and all are accurate quotations. My harnesses are in `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/` as `v1_realtime_cadence.cpp`, `v2_window_probe.cpp`, `v3_exact_order.cpp`, `v4_uniqueptr_null.cpp`, `v5_badmodel.cpp`, `v6_loglevel.cpp`, `v7_disclosure.cpp`, `v8_realspeech.cpp`.

# Verdict table

| # | Verdict | Reported | Agreed | Basis |
|---|---|---|---|---|
| VP-1 | **CONFIRMED — mechanism partly wrong, frequency overstated ~10x** | SEV-1 | **SEV-1** | reproduced with my own harness at real cadence |
| VP-2 | **CONFIRMED — broader than reported; one reachability claim wrong** | SEV-1 | **SEV-2** | reproduced 4 ways, own stack |
| VP-3 | **CONFIRMED mechanism / OVERSTATED severity / fix claim REFUTED** | SEV-1 | **SEV-3** | reproduced; [unk] gain does not replicate |
| VP-4 | **CONFIRMED, latent** | SEV-2 | **SEV-3** | 4/6 SIGABRT; unreachability independently re-verified |
| VP-5 | **CONFIRMED as a guard defect; consequence still unproven** | SEV-2 | **SEV-4** | reproduced; trigger needs write access to the build tree |
| VP-6 | **CONFIRMED — worse than reported** | SEV-2 | **SEV-3** | my max overrun 75 ms, not 22.8 |
| VP-7 | **CONFIRMED, latent** | SEV-3 | **SEV-4** | TSan reproduced; single call site, pre-`start()` |
| VP-8 | **CONFIRMED as a doc gap only** | SEV-4 | **SEV-4** | code reading; no defect demonstrated |
| VP-9 | **CONFIRMED — and its premise is unsupported** | SEV-4 | **SEV-4** | reproduced; disclosure premise disproved |

---

## VP-1 — CONFIRMED, SEV-1. Two sub-claims are wrong.

**What I verified myself.** `app_shell.hpp:88-90` declares `voice_, engine_, voiceGate_`; `~AppShell` (`app_shell.cpp:54-56`) calls only `teardownWorkers()`; `grep -n "voice_\|engine_\|voiceGate_" src/ui/app_shell.cpp` shows every use is inside `armVoice()` — nothing stops the pipeline at shutdown. Note the *failure* path at `app_shell.cpp:108-110` already resets in the correct order (`voice_` first); only the destructor gets it backwards.

I wrote `v3_exact_order.cpp` — shipped member order, no explicit `reset()` anywhere, decode lambda copied verbatim from `app_shell.cpp:94-96` — and `v8_realspeech.cpp`, which streams the real speech corpus through it at a true 10 ms device cadence:

```
real speech, 10 ms cadence, 20 runs x 4 teardowns : 0 139 0 0 139 139 139 0 0 0 0 0 0 0 0 139 0 0 0 139
                                                     -> 6/20 runs crashed, 80 teardowns
under ASan: ASSERTION_FAILED (VoskAPI:PruneForwardLinks():lattice-incremental-decoder.cc:259)
```
Re-running the reporter's own harnesses: `r1_shutdown_order-plain` 5/8 runs SIGSEGV, `r1_control_order-plain` 0/8; the two files differ only in declaration order (`diff` confirms).

**REFUTED sub-claim:** *"`~unique_ptr` does not null its stored pointer, so the `engine_ ?` guard is truthful-looking and useless."* False on this toolchain. `v4_uniqueptr_null.cpp` prints `inside ~Probe: unique_ptr reads as NULL (guard effective)` — libc++'s `~unique_ptr` calls `reset()`, which stores null *before* invoking the deleter. My instrumented `v2_window_probe.cpp` confirms the consequence: every post-free decode call read `engine_ == nullptr` and returned safely (`post-free-with-nonnull-ptr=0`). The real defect is therefore the *narrower* in-flight race — a `feed()` that began before the null store and is still executing while `vosk_recognizer_free`/`vosk_model_free` run — not a guaranteed dangling call.

**OVERSTATED sub-claim:** "12/16 Release runs", "one to four callbacks land inside the window *every time*". `r1`'s fake device delivers a 10 ms buffer every **200 µs** — 50x real time — so the decoder is essentially always inside `feed()`. At the true cadence I measured the free window at 7.8–18.6 ms and got **~1 post-free callback per teardown**, and with synthetic audio **1 crash in 164 teardowns (0.6%)**. Only real speech (which drives the slow final-result path) raises it to **7.5% per teardown**. Real, worth fixing, but not "every quit".

**Smallest fix** (order-independent, one line):
```cpp
AppShell::~AppShell() { if (voice_) voice_->stop(); teardownWorkers(); }
```
Declaring `voice_` last also works — 0 crashes in 18 control runs across both cadences.

## VP-2 — CONFIRMED and broader, SEV-2. One reachability claim is wrong.

`v5_badmodel.cpp` damages the model **on disk before the run** (no TOCTOU) and then executes `armVoice()`'s exact lines 76-85. All four damage modes make `prepareRecognizer` return `error=0` and then kill the process:

| `graph/HCLr.fst` state | `prepareRecognizer` | `VoskEngine::start()` |
|---|---|---|
| 4096 bytes of 0x7f | 0 (safe to arm) | **exit 139** |
| truncated to half (valid prefix) | 0 | **exit 139** |
| zero length | 0 | **exit 139** |
| right size, all zero bytes | 0 | **exit 139** |

My ASan stack:
```
SEGV on unknown address 0x000000000000 ... READ
 #0 kaldi::LatticeIncrementalDecoderTpl<...>::InitDecoding()
 #2 Recognizer::Recognizer(Model*, float, char const*)
 #3 vosk_recognizer_new_grm
 #4 pptv::VoskEngine::start(...)  vosk_engine.cpp:86
```
This is stronger than reported — the report found one mode, there are at least four, including the plain truncation an interrupted copy produces.

**Correction to the reachability argument.** The report says "an interrupted extract … produces exactly *file exists, content wrong*" and "the build will not repair it". The first half is wrong: `build/build.ninja:684` chains `tar xf … && cmake -E touch …/model.stamp`, so an interrupted extract never writes the stamp and the *next build does re-extract*. Only damage occurring **after** a successful extract (bad restore, disk error, cloud-sync eviction, manual meddling) is unrepairable. That is a real but uncommon state, which is why I put this at SEV-2 rather than SEV-1. Given the state, the crash is 100% deterministic and lands inside `armVoice()` after the fullscreen window is up.

**Smallest fix:** make `modelIsGrammarCapable()` read the OpenFst magic instead of calling `QFileInfo::exists`. The good files start `d6 fd b2 7e` (0x7eb2fdd6) — verified with `xxd` on both `HCLr.fst` and `Gr.fst`. Four bytes turn the SIGSEGV into `ModelNotGrammarCapable`, which the UI already handles.

## VP-3 — mechanism CONFIRMED, severity OVERSTATED, proposed fix REFUTED. SEV-3.

Reproduced the decode behaviour exactly: `corpus_talk` 30 utterances / **0 dispatched**, `narration` 10 / 0, `audience_talk` 10 / 0, positive control `commands_spaced` **5/5 dispatched** correctly. So yes — `grammarJson()` emits no out-of-grammar token, and every utterance becomes grammar words. The `vosk_recognizer.hpp:13-16` claim ("must be able to produce ONLY the five commands") is inaccurate as written.

But the safety *outcome* held in 50/50 of my non-command utterances, as it did in the reporter's 80. There is no demonstrated path from salad to a dispatch. That is a documentation/defence-in-depth defect, not a SEV-1.

**The `[unk]` fix claim does not reproduce.** Report: "40 → 23 … strictly better at no recognition cost". Mine:

| corpus | shipped | with `[unk]` |
|---|---|---|
| corpus_talk (30 sentences) | 30 utterances | 29 |
| audience_talk | 10 | **11** |
| narration | 10 | **12** |
| commands_spaced | 5 dispatched | 5 dispatched |

Utterance count went **up** on two of three corpora. Worse, `[unk]` makes the residue *shorter and closer to a bare command* — `"[unk] continue the presentation"`, `"[unk] the next slide [unk]"` — where the shipped grammar produced long unmatched salad (`"resume thirty forty continue the presentation resume pause"`). `matchCommand()` does not strip `"[unk]"` today, so it is safe today, but the change moves the failure surface toward the matcher rather than away, and would be actively dangerous alongside any normalisation that drops unknown tokens.

**The finding names the wrong weak defence.** TM-002 is "the AUDIENCE moves the presenter's slides". I synthesised 10 audience utterances in a different voice (`aud2/audience_commands.raw`) and ran them through the real pipeline:

```
HEARD: "next slide"              -> DISPATCH NextSlide(0)
HEARD: "previous slide"          -> DISPATCH PreviousSlide(0)
HEARD: "pause the presentation"  -> DISPATCH PausePresentation(0)
HEARD: "continue the presentation" -> DISPATCH ContinuePresentation(0)
HEARD: "go to slide seven"       -> DISPATCH GoToSlide(7)
DISPATCHED=5   (identical with and without [unk])
```
Anyone in the room who says a command moves the deck. The grammar is irrelevant to that and `[unk]` does not touch it. If TM-002 is the concern, this — not the salad — is the finding.

## VP-4 — CONFIRMED, latent. SEV-3.

`r2_rearm-asan`, 6 runs: `134 134 134 134 0 0` — 4/6 abort with `ASSERTION_FAILED (VoskAPI:CuSubMatrix():cudamatrix/cu-matrix-inl.h)`. Mechanism is real: `app_shell.cpp:80` swaps the pointer to the new engine, deletes the old while the audio thread may be inside it, then spends the model load calling `start()` on an engine the audio thread is concurrently feeding.

I re-verified unreachability independently and agree: `armVoice` has exactly one caller (`app_shell.cpp:317`); `openDeck` callers are `main.cpp:59`, `app_shell.cpp:121`, `StartView::fileDropped`; `start_` is hidden at `app_shell.cpp:275-277`; the Open shortcut is a default-context (`Qt::WindowShortcut`) `QShortcut` on the hidden StartView (`start_view.cpp:61`); and `grep -rn "FileOpen\|QFileOpenEvent" src/` returns **nothing**, so macOS "Open With" cannot supply a second deck either. Not reachable today → SEV-3, not SEV-2.

## VP-5 — CONFIRMED as a guard defect. SEV-4.

Reproduced `r6_grammar_guard-plain hclg`: `prepareRecognizer -> error=0` with `graph/HCLG.fst` present. The guard at `vosk_recognizer.cpp:98-99` genuinely never checks the condition its own comment names. `start()` then fails only on the incidental missing `graph/words.txt` — exactly as reported, and the full-vocabulary consequence remains unproven (the reporter labelled it HYPOTHESIS; I could not test it either without a real static-graph model).

I downgrade to SEV-4 on exploitability: `PPTV_VOSK_MODEL_DIR` is a compile-time absolute path into the build tree, the vendored zip contains no `HCLG.fst`, so producing this state requires write access to the build directory — at which point the binary itself is writable. Still worth the one-line `&& !QFileInfo::exists(graph.filePath("HCLG.fst"))`.

## VP-6 — CONFIRMED, worse than reported. SEV-3.

The contract violation is plain from the source: `miniaudio_capture.cpp:100-110` invokes `sink_` inline **while holding `sinkMutex_`**, and `voice_pipeline.cpp:47-69` then allocates two vectors, runs the nnet3 decode, builds a `QString` and posts a `QMetaCallEvent` — on that thread. `voice_pipeline.hpp:22-24`'s "decoded on a dedicated worker" is simply false; there is no worker.

My `r7_rt_budget-plain` numbers exceed the report's:

| corpus | period | median | p95 | p99 | **MAX** | over budget |
|---|---|---|---|---|---|---|
| commands_spaced | 160 (10 ms) | 0.015 | 0.042 | 1.508 | **32.197 ms** | 3/1210 |
| corpus_talk (109 s) | 160 (10 ms) | 0.015 | 0.034 | 1.015 | **75.058 ms** | 15/10940 |
| commands_spaced | 171 (10.69 ms) | 0.015 | 0.043 | 1.078 | **39.017 ms** | 2/1132 |

75 ms is seven CoreAudio periods, not 2.1. But the consequence is dropped input immediately after a recognised utterance — degraded recognition, not a crash — so SEV-3.

## VP-7 — CONFIRMED, latent. SEV-4.

`r5_races-tsan decoder` reproduced both reports: `data race ... setDecoder voice_pipeline.cpp:21` vs `onSamples voice_pipeline.cpp:61`, plus `data race on vptr (ctor/dtor vs virtual call)`. `capture`, `restart` and `destroy` modes were clean, as reported. `grep -rn "setDecoder" src/` shows one call site (`app_shell.cpp:94`), on the GUI thread, before `start()` at line 106 — unreachable in the shipped wiring. SEV-4.

## VP-8 — CONFIRMED as documentation only. SEV-4.

`audio_capture.hpp:47` is a bare `virtual void stop() = 0;` with no drain requirement, and `voice_pipeline.hpp:55-57` does destroy `decode_` before `capture_`. But `~VoicePipeline`'s **body** calls `stop()` first (`voice_pipeline.cpp:11-13`), so with any draining backend the member order never matters. Correctly filed as a contract-doc gap; no defect demonstrated, and I did not attempt to demonstrate one.

## VP-9 — CONFIRMED, and its premise is unsupported. SEV-4.

`v6_loglevel-plain` puts markers either side of the `VoskEngine` constructor:
```
>>> constructing VoskEngine (this calls vosk_set_log_level(-1))
>>> log level is now -1. Anything below is NOT silenced.
ERROR (VoskAPI:Model():model.cc:122) Folder '/nonexistent/model' does not contain model files...
```
So `vosk_set_log_level(-1)` does not suppress `KALDI_ERR`/`KALDI_WARN`. **Additional finding the report missed:** I raised the level back up and decoded 12 s of real command speech (`v7_disclosure-plain commands_spaced.raw {-1,0,2}`). Level 2 emitted 18 stderr lines and **zero** containing `slide|presentation|next|previous|pause|continue`. The comment's premise — "it would print heard words" — is not demonstrated at any level I tested. The mitigation is ineffective against what Vosk actually prints (paths, decoder internals) and unnecessary for what it was written to stop.

## Not re-verified

I did not re-run the leak/RSS negative result (`r4_leak`), and I did not attempt VP-8's non-draining-backend crash. No microphone exists on this Mac mini (`system_profiler SPAudioDataType` lists output devices only), so every capture path — mine and the reporter's — is exercised through a fake device; that limitation is unavoidable here and the reporter stated it honestly.

---

**TL;DR (plain English):** Nine problems were reported; all nine are genuinely there, but three were oversold and one recommended fix does not hold up. The crash-on-quit is real — I got it to crash 6 times out of 20 with realistic audio — but the original claim that it happens almost every time came from a test that fed the app audio fifty times faster than a real microphone, and the explanation of *why* it crashes was wrong in a way I proved with a five-line program. The damaged-speech-file crash is real and actually worse than reported (four different kinds of damage all crash it), though the claim that rebuilding can't fix it is only half true. The "the microphone can only hear five commands" problem is real as a wrong comment in the code, but nothing bad ever actually happened in 50 tries, and the one-word fix suggested for it did not improve anything when I measured it — meanwhile the genuinely worrying thing is simpler and untested by the original report: anyone in the audience who says "next slide" out loud moves the slides, which I demonstrated. The rest are smaller than labelled: three of them can't happen in the app as it ships today.