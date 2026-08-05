All ten findings reproduced. Compiling the verdict.

---

# UAT-4 SKEPTIC REVIEW — audio layer @ `cd11cf8` (audio sources byte-identical at HEAD `89004de`; `git diff cd11cf8 HEAD` touches only `WALK-STATE.md` and `.claude/process-state.json`)

## The one correction that reframes everything

**`makeMiniaudioCapture()` has zero callers.** Not in `src/`, not in `tests/`.

```
$ grep -rn "makeMiniaudioCapture" src tests CMakeLists.txt
src/audio/miniaudio_capture.hpp:19:std::unique_ptr<IAudioCapture> makeMiniaudioCapture();
src/audio/miniaudio_capture.cpp:124:std::unique_ptr<IAudioCapture> makeMiniaudioCapture() {
```
The only reference to the capture layer outside `src/audio/` is a **comment** at `src/command/recognizer_controller.hpp:15` ("The Vosk + miniaudio adapter implements…" — it does not exist yet). `tests/test_audio_capture.cpp` uses `FakeCapture`, never the real class. `RecognizerController` takes an abstract phrase source and has no audio include.

So **AUD-1…AUD-7 are all latent**. At HEAD nothing constructs the device, so none of them can "ruin a live talk" — the SEV-1 definition. The tester disclosed this for AUD-6 only, then stamped SEV-1 on AUD-1/2/3. Every finding is *real*; the severities are inflated by one level across the board. They become live the instant F8c wires a caller, which WALK-STATE says is next — so this is timely work, just mis-graded.

## Verdict table

| # | Verdict | Tester | Agreed | My own evidence | Smallest fix |
|---|---|---|---|---|---|
| **AUD-1** Null-backend silent success | **CONFIRMED** (+strengthened) | SEV-1 | **SEV-2 latent → SEV-1 on wire-up** | My `s1_null`: `start()→None`, `isRunning()=1`, 150 callbacks / 144 000 samples, **NONZERO=0**. My `s2_backend`: selected backend `Null` (enum 14), CoreAudio-only init `-2`, **0 capture devices** | `#define MA_NO_NULL` beside `miniaudio_capture.cpp:7-11` |
| **AUD-2** double-`stop()` hang | **CONFIRMED** | SEV-1 | **SEV-2 latent** | `-O3 -DNDEBUG`: F1 *and* F2 both hang at **round 0** (25 s watchdog). `-O0` no-NDEBUG: `Assertion failed … miniaudio.h, line 44367`. Release-hides-it claim verified | `std::atomic<bool> initialised_` + a `startStopMutex_` around `start()`/`stop()` |
| **AUD-3** mutex starves UI thread | **CONFIRMED** (magnitude understated) | SEV-1 | **SEV-2 latent** | My `s4`: mutex-free window collapses to **0.002 ms** once sink > period. My `s5` (busy-wait, 40 trials): max 954 ms. Re-ran *their* `h7` 12×@60 ms myself: **563 / 1875 / 2094 / 2185 / 2590 / 3696 / 5211 / 5514 / 11021 / 13315 / 14452 ms, and one exceeded the 15 s watchdog** | Copy the sink under the lock, invoke it **outside** |
| **AUD-4** sink re-entrancy deadlock | **CONFIRMED** | SEV-2 | **SEV-2 latent** | D3 (`stop()` from sink) hung **>120 s**; D4 (`setSink()` from sink) hung 15 s. Neither printed its "returned" line | Document as forbidden, or `std::recursive_mutex` + a `stop()` guard |
| **AUD-5** exception → `terminate()` | **CONFIRMED** | SEV-2 | **SEV-2 latent** | `exit=134`, `libc++abi: terminating due to uncaught exception of type std::bad_alloc`. Contract contradiction verified by signature: `audio_capture.hpp:37-39` forbids RT allocation, `audio_format.hpp:78` returns `std::vector` **by value** | `try { sink_(…) } catch (...) {}` in `onData` |
| **AUD-6** `start()` check-then-act | **CONFIRMED** (E2 flakier than stated) | SEV-2 | **SEV-2 latent** | E1 (4× concurrent `start()`) hangs at **round 0**. E2 (`start` vs `stop`) is **intermittent — 2/5 attempts hung**, 3/5 survived 50 rounds; presented as a clean hang | `compare_exchange` on `running_` before `ma_device_init` |
| **AUD-7** no device-loss detection | **CONFIRMED** (by absence) | SEV-2 | **SEV-2 latent** | `grep notificationCallback src/` → nothing. `DeviceFailed` returned **only** at `miniaudio_capture.cpp:59` (initial start). miniaudio does offer it: `miniaudio.h:7115` | Set `cfg.notificationCallback` |
| **AUD-8** UB inside the BUG-56 guard | **CONFIRMED** | SEV-3 | **SEV-4** — unreachable-squared | UBSan: `audio_format.cpp:94:81 runtime error: addition of unsigned offset … overflowed`, `exit=134`. But needs a caller passing `SIZE_MAX` for *both* args; no caller exists and per AUD-9 no realistic one can | `if (frameCount > sampleCount) return {};` |
| **AUD-9** guard structurally unreachable | **CONFIRMED** | SEV-3 | **SEV-3** | My sweep: **522 272 combos, guard fired 0 times**. Mid-stream 8ch→2ch: **accepted**, produced 43 samples of scrambled garbage — `audio_format.hpp:71-77` claims it catches exactly this | Widen `CaptureSink` to carry `frameCount` + `AudioFormat` |
| **AUD-10** `NoDevice` unreachable | **CONFIRMED** | SEV-3 | **SEV-4** cosmetic | Only `audio_capture.hpp:32` + `audio_capture.cpp:14` (+ test enum lists). Never returned | Falls out of the AUD-1 fix |
| *note* `downmixToMono` unbounded | **CONFIRMED** | SEV-4 | **SEV-4** | Signature `audio_format.hpp:57-58` takes no buffer length; reads `frameCount*channels` unchecked | Add a `sampleCount` parameter |

## Where I went further than the tester

**AUD-1's `PermissionDenied` hypothesis is stronger than they claimed — it is verified, not hypothetical.** I read the fallback loop at `miniaudio.h:44054-44092` and it falls through on **device**-init failure, not merely context failure:
```c
result = ma_context_init(&pBackendsToIterate[iBackend], 1, pContextConfig, pContext);
if (result == MA_SUCCESS) {
    result = ma_device_init(pContext, pConfig, pDevice);
    if (result == MA_SUCCESS) { break; } else { ma_context_uninit(pContext); }
}
```
and I measured that Null-only init **always** succeeds:
```
Null-ONLY init -> 0 (SUCCESS)   => PermissionDenied branch is UNREACHABLE
```
So `return CaptureError::PermissionDenied` at `miniaudio_capture.cpp:44` is dead for **any** CoreAudio failure reason. No TCC prompt needed to establish it. Three of the five `CaptureError` values (`PermissionDenied`, `NoDevice`, and in practice `UnsupportedFormat` — Null reports a valid 48 kHz/2 ch) are unreachable while Null is compiled in. Worth noting the team already built the permission path: `cmake/MacOSXBundleInfo.plist.in:20` carries `NSMicrophoneUsageDescription`, and `CMakeLists.txt:121-124` explains why. The infrastructure is right; the Null fallback swallows the signal.

## Caveats the tester should have stated

- **AUD-3's magnitude is Null-backend-only.** `ma_device_read__null` (`miniaudio.h:21340-21356`) waits with `ma_sleep(10)` *only while ahead of schedule*; once the sink overruns, `currentFrame >= targetFrame` is already true and it re-enters the callback with **zero** delay. That zero-sleep spin is what drives the mutex-free window to 2 µs. On real CoreAudio the HAL paces the thread and the magnitude is **unverified** — this machine has no mic to test it. The *mechanism* (unfair `std::mutex` held across arbitrary user code) is real regardless; the "up to 9.9 s" number should be quoted as Null-backend. It is fair to add that on a mic-less or permission-denied machine the Null backend *is* production, per AUD-1.
- **AUD-6 E2 is flaky, not deterministic** (2/5 for me). E1 is the solid one.
- The report's "common build prefix" is not runnable as printed — `AUD="$SRC/src/audio/…"` unquoted with a space in `$SRC` word-splits and fails. Their harness files and binaries do exist and I re-ran `h7` successfully, so the testing was real; only the transcription is broken.

## Claims I checked that hold up

Their "credit where due" is **honest** — I rebuilt the real `tests/test_audio_format.cpp` against a mutant with the guard (`audio_format.cpp:89-92`) deleted:
```
tests/test_audio_format.cpp:173: FATAL ERROR: test case CRASHED: SIGABRT
  "the device raises its channel count between describing and delivering"
[doctest] test cases: 11 | 10 passed | 1 failed
```
The test genuinely catches the fix's removal.

**Not verified by me** (outside the audio surface / accepted as stated): their `format_`-staleness "NOT a bug" note, and the BUG-40 `test_deck_load_worker.cpp:141` flake rate.

## Revised fix order

AUD-1 (one `#define`; also resurrects three error codes) → AUD-2 + AUD-6 (one `startStopMutex_` + atomic `initialised_` fixes both) → AUD-3 (copy sink, call outside lock) → AUD-5 (`try`/`catch`, 2 lines) → AUD-7 → AUD-9. **All of it before F8c wires a caller** — that is the moment every one of these stops being latent.

---

**TL;DR (plain English)**

The tester found ten real problems and I could reproduce every single one — nothing was made up, and their numbers were honest (one of their scariest measurements I actually reproduced *worse* than they reported).

But they missed the most important fact about their own findings: **the microphone code isn't plugged into the app yet.** Nothing in the program ever switches the microphone on. So none of these problems can spoil a talk today, because there is no voice feature running at all. They labelled three of them "worst possible severity — ruins a live talk", and that isn't right yet. Everything drops one notch.

That said, this is not a reason to relax. The project's own notes say wiring the microphone up is the very next job, and the moment that happens all of these go live at once. So the honest framing is: fix them now, while they're cheap, rather than fix them in five days under pressure.

The standouts are unchanged. The app would tell the presenter "microphone is working" when it absolutely isn't — and I proved that's worse than the tester thought: the helpful "microphone is off" message they wrote can *never* appear, no matter why the microphone fails. That's a one-line fix. The freeze-on-quit and the up-to-ten-second keyboard freeze are both genuine, though I'd note the ten-second figure was measured against a fake microphone and might be milder on real hardware — nobody can check that on a machine with no microphone.

One nice thing: I deliberately sabotaged an earlier bug fix to see whether the project's test would notice. It did, immediately. That part of the work is solid.