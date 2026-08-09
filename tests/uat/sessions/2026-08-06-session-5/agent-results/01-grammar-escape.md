# UAT-5 — LENS: CAN ANYTHING SAID IN THE ROOM ESCAPE THE PROCESS?

Method: built the vendored model + `libvosk` and drove the **real decoder** with synthesised speech (`/usr/bin/say` → 16 kHz mono LEI16), through the **real** `VoskEngine` → `RecognizerController` → `PresentationController`. ~260 utterances, 9 voices, 3 speaking rates. Probes at `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/{probe,scenario,timing}.cpp`.

---

## The assigned lens: NO DISCLOSURE CHANNEL FOUND — VERIFIED

I tried to break the claim and could not. Reporting it as a clean result, with evidence:

| Channel | Result | Evidence |
|---|---|---|
| stdout/stderr of the product path | **0 bytes**, incl. model load + 5 Q&A utterances | `out_product_qa.txt`, `stderr_qa.txt` |
| The channel is real and the mitigation is load-bearing | With `vosk_set_log_level` **not** called: **1900 bytes** to stderr; at level 2, 2399 bytes | `stderr_raw.txt`, `stderr_rawloud.txt` |
| Even when loud, does Vosk print heard text? | No — word **IDs** only (`Changing word 55014 to 55019`), never text | `stderr_rawloud.txt` |
| Files created/written | **None.** `lsof` during decode shows no writable fd but my own output file; model dir, bundle, `$TMPDIR`, `/tmp`, `~/Library` all unchanged | `lsof_snap.txt`, marker-based `find` sweeps |
| Whole-`$HOME` + `/tmp` + `/var/folders` sweep during decode | Only OS-owned background logs (wifi/launchd/powermanagement); `grep -c` for the app in `system.log` = **0** | marker3 sweep |
| Real app (`powerpoint_voice`, deck loaded, RSS 182 MB = model resident) | stderr = 2 lines of Qt offscreen/font noise, no Vosk output; **macOS unified log: 0 entries** for the process | `app_stderr.txt`, `log show` |
| Crash dump | Existing `powerpoint_voice-2026-08-04-190008.ips` parsed: registers, frames, `instructionByteStream` only. **No heap strings, no `asi` field**, paths redacted by macOS to `/Users/USER/Documents/*/` | header/body key dump |
| Phrase lifetime | `VoicePipeline::onSamples` → queued lambda → `AppShell` lambda → `voiceGate_->onPhrase()` → `matchCommand` local. Result is `Command{enum,int}`. No member, no property, no widget, no `lastNotice_` (built from `Notice{id,int,int}`) | static trace |
| Heard **numbers** reaching the projector | Bounded: out-of-range shows `slideCount_` not the heard number (`presentation_controller.cpp:61`); `CommandEcho` only fires on a jump that actually happened | source + notice.cpp |

Two residuals, memory-only, **not** SEV-1 by the stated bar: `libvosk` retains the last result JSON until the next `vosk_recognizer_result`, and freed QString buffers are not zeroed.

**But the lens turned up something worse than a leak.**

---

## SEV-1 (VERIFIED) — Ordinary English defeats the Q&A pause gate and moves the deck

Real audio, real decoder, real gate, real controller, 20-slide deck on slide 5:

```
GATE     SPOKEN IN THE ROOM                      DECODER EMITTED               RESULT
ACTIVE   PRESENTER: "pause presentation"         [pause presentation]          GATE -> PAUSED
PAUSED   AUDIENCE Q: "thanks, great talk, my question is about the hiring freeze"
                                                 [six the one go eighteen...]  (no effect)
PAUSED   AUDIENCE: "next point"                  [next slide]                  (no effect)   <- gate holds
PAUSED   AUDIENCE: "review the presentation"     [resume the presentation]     GATE -> ACTIVE
ACTIVE   AUDIENCE: "next point"                  [next slide]                  DECK MOVED 5 -> 6
```

`"review the presentation"` decodes to exactly `"resume the presentation"` in **3/3 voices**. **4 of 8** ordinary non-command phrases un-pause the gate (`review the presentation`, `reviewing the presentation`, `we saw the presentation`, `use the presentation`), and in every one of those the follow-on `"next point"` then moved the deck.

This is TM-002/TM-019 realised exactly as written: the audience moves the presenter's slides, during Q&A, with the protection engaged.

### Root cause (VERIFIED, and it falsifies the file's central claim)

`src/command/vosk_recognizer.hpp` states: *"the decoder must be able to produce ONLY the five commands. Not 'usually produce', not 'produce them best' — only."*

That is false. Vosk's own log gives it away:

```
LOG (VoskAPI:Estimate():language_model.cc:142) Estimating language model with ngram-order=2, discount=0.5
LOG (VoskAPI:OutputToFst():language_model.cc:209) Created language model with 40 states and 118 arcs.
```

`vosk_recognizer_new_grm` builds a **bigram word-loop over the union of the words** (40 states = the 40 distinct grammar words), not a phrase-alternative FST. So the decoder can and does emit arbitrary sequences — real output from Q&A audio:

```
[twelve ten eleven eleven pause forty continue zero one resume the thirty forty]
[hundred slide zero thirty three nine go to four pause the four to seven]
```

Compounding it: the grammar omits Vosk's `"[unk]"` token, so out-of-vocabulary speech has **no sink** and is force-mapped onto the nearest grammar words. The safety property is not enforced by the grammar at all — it is enforced one layer downstream by `matchCommand`'s exact phrase equality, and that layer is thinner than a 40-word closed vocabulary suggests.

### False-trigger rate (VERIFIED)

105-utterance battery of ordinary speech, 3 voices: **12/105 (11%) fired a slide command.**

| Spoken | Fired | Decoded as |
|---|---|---|
| "next point" | **3/3** | `next slide` → NextSlide |
| "next slot" | **3/3** | `next slide` → NextSlide |
| "precisely" | 2/3 | `previous slide` → PreviousSlide |
| "excited" | 2/3 | `next slide` → NextSlide |
| "exactly" | **14/27** (9 voices × 3 rates) | `next slide` → NextSlide |
| "because of the presentation" | 1/3 | `pause the presentation` → Pause |
| "throughout the presentation" | 1/3 | `resume the presentation` → Continue |

A presenter saying **"my next point is…"** advances their own slide. `UAT-4 applied 33 mutations and killed none` — no test in `tests/test_vosk_engine.cpp` feeds the decoder anything but zeroed silence, which is why this survived.

**Suggested direction** (author's call): add `"[unk]"` to `grammarPhrases()` so OOV speech has a sink; and make un-pause not reachable from one mishearing — `command_matcher.cpp` already argues the residual "stuck in Paused" risk *"is covered by keyboard parity (F6)"*, so making Continue keyboard-only is consistent with the design's own reasoning.

---

## SEV-3 (VERIFIED) — Vosk inference runs on the CoreAudio real-time callback thread

`audio_capture.hpp` states the contract: *"Audio arrives on a REAL-TIME thread… no allocation, no locks, no Qt objects, no logging."* `voice_pipeline.hpp` claims *"samples are converted and decoded on a dedicated worker."*

**There is no worker.** `miniaudio_capture.cpp:onData` is `cfg.dataCallback` and calls `sink_` directly; `VoicePipeline::onSamples` then does, on that thread: `std::vector` allocation (`toRecognizerFormat`), full Vosk inference (`decode_`), `QString` construction, and `QMetaObject::invokeMethod` (event-queue mutex + `QMetaCallEvent` allocation).

Measured (`timing.cpp`, 512-frame @48 kHz callback = **10.67 ms deadline**):
```
median 0.01 ms   p99 1.68 ms   WORST 21.40 ms   -> 2x the deadline
```
The overrun lands on the utterance-final call — i.e. a dropped buffer at exactly the moment a command completes. Bounded today on this Mac; it degrades under the pre-render load and thermal conditions of a live talk.

---

## SEV-3 (HYPOTHESIS) — re-arming voice races the audio thread on `engine_`

`AppShell::armVoice` assigns `engine_ = std::make_unique<VoskEngine>()` **before** replacing `voice_`. The old `VoicePipeline` is still running and its decoder lambda reads the `AppShell::engine_` member from the audio thread — so the old `VoskEngine` is destroyed (`vosk_recognizer_free`/`vosk_model_free`) while that thread may be inside `feed()`. Data race + use-after-free. I could not find a shipped UI route to a second successful `openDeck` (the start view is only `hide()`n), so I am labelling this latent, not confirmed reachable.

## SEV-2 (VERIFIED) — the shipped bundle contains neither the engine nor the model

`build/powerpoint_voice.app` is **652 K**: `Info.plist` + the binary, nothing else. `PPTV_VOSK_MODEL_DIR` is baked to an absolute build-tree path and the rpath points at `build/vosk`. The `.app` is not movable off this machine; anywhere else, voice fails closed with "The speech model is missing." Off-lens, but it is what would be handed to the presenter.

---

**TL;DR (plain English):** The good news first — I could not find any way for words spoken in the room to escape the program. Nothing is written to any file, any log, or the screen; I checked the system log, temp folders, the whole home directory, and an old crash report, and all were clean. The bad news is bigger. The speech recogniser is not actually locked to the five commands the way the code claims — it is locked only to the *forty words* those commands are made of, and it can shuffle them into any order. So everyday words get heard as commands: "my next point is…" advances the slide (3 times out of 3), and "exactly" advances it about half the time. Worst of all, during audience Q&A — when slide control is supposed to be switched off — someone saying "review the presentation" is heard as "resume the presentation", which switches control back on, and the very next stray word moves the deck. I reproduced that whole sequence end to end with real audio. There are also two smaller problems: the speech processing runs inside the microphone's time-critical audio loop, which it is explicitly not supposed to do, and the finished app package doesn't actually contain the speech engine or its model, so it only works on this one machine.