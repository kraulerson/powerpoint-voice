All harnesses complete. Writing up.

---

# UAT-4 — Audio layer (`src/audio/*`), adversarial

**Surface:** `audio_format.{hpp,cpp}`, `audio_capture.{hpp,cpp}`, `miniaudio_capture.cpp` @ `cd11cf8`. No project file was modified; all work in `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4/`.

**Key environmental fact I exploited:** this Mac mini has **0 capture devices**, so miniaudio silently falls back to its **Null backend** — which still runs a worker thread and calls the data callback. That gave me a live callback source to attack the real `MiniaudioCapture` object with, on a machine with no microphone.

Common build prefix for every command below:
```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
SRC="/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice"
FW="-framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreFoundation"
AUD="$SRC/src/audio/miniaudio_capture.cpp $SRC/src/audio/audio_capture.cpp $SRC/src/audio/audio_format.cpp"
```

---

## AUD-1 — SEV-1 — VERIFIED — `start()` returns success with **no microphone at all**, and feeds the recogniser pure silence forever

`ma_device_init(nullptr, &cfg, &device_)` (miniaudio_capture.cpp:40) passes a **NULL backend list**. `ma_device_init_ex` (miniaudio.h:44055-44092) then iterates *every* backend and `ma_backend_null` is last in the list. Null **always** initialises.

```
clang++ -std=c++20 -O2 -I"$SRC/third_party/miniaudio" h4_backend.cpp $FW -o h4_backend && ./h4_backend
```
```
ma_device_init -> 0 (MA_SUCCESS)
SELECTED BACKEND : Null   (enum 14; ma_backend_null == 14)
user  format     : 16-bit Signed Integer, 2 ch, 48000 Hz
CoreAudio enumeration: 1 playback device(s), 0 CAPTURE device(s)
CoreAudio-only capture device init -> -2 (FAILED -> production code falls through)
```

Through the real production API (`h3_null.cpp`, identical build + `$AUD`):
```
start()          -> None
isRunning()      -> 1
deviceFormat()   -> {rate=48000, channels=2}  isValid=1 matchesRecognizer=0
after 1.5 s of 'capture':
  sink callbacks     = 151
  samples delivered  = 144960
  NON-ZERO samples   = 0   <-- pure silence
```

Consequences:
- The entire `CaptureError` vocabulary is bypassed. `NoDevice` is **dead code** — `grep -rn "NoDevice" src/` returns only the enum declaration and its message string.
- **`PermissionDenied` is almost certainly dead too.** The production comment at miniaudio_capture.cpp:41-44 asserts "macOS reports a refused microphone as an init failure". A `ma_device_init` failure is exactly the branch that falls through to Null. So on the presenter's MacBook Pro, tapping **"Don't Allow"** yields `CaptureError::None`, `isRunning() == true`, and silence — the carefully-worded message *"Microphone access is off… the keyboard still controls the deck"* never appears. (VERIFIED for no-device; **HYPOTHESIS** for permission-denied, since this machine cannot raise a TCC prompt — but it is the same code branch.)
- This directly contradicts miniaudio_capture.hpp:11-13 ("binds to the DEFAULT input device through miniaudio").

**Fix:** `#define MA_NO_NULL` alongside the other `MA_NO_*` defines at miniaudio_capture.cpp:7-11, or pass an explicit `ma_backend_coreaudio` list. One line.

---

## AUD-2 — SEV-1 — VERIFIED — `stop()` racing `stop()` or the destructor **hangs the process forever** in the Release configuration

`stop()` gates on `if (!initialised_) return;` where `initialised_` is a **plain `bool`** (miniaudio_capture.cpp:114). Two threads both pass the gate and both run `ma_device_stop` + `ma_device_uninit` on the same device.

Release flags (`-O3 -DNDEBUG`, matching `CMAKE_BUILD_TYPE=Release`):
```
clang++ -std=c++20 -O3 -DNDEBUG -g -I"$SRC/src" -I"$SRC/third_party/miniaudio" h8_dblstop.cpp $AUD $FW -o h8_rel
./h8_rel        # F1: two threads call stop()
./h8_rel f2     # F2: destructor races stop()
```
```
[F1] stop() racing stop() on one object (300 rounds)
  *** WATCHDOG: HUNG for 25 s at 'F1 double stop' (round 0) ***

[F2] destructor racing stop() (300 rounds)
  *** WATCHDOG: HUNG for 25 s at 'F2' (round 0) ***
```
Both hang on **round 0**. Without `-DNDEBUG` it aborts instead:
```
Assertion failed: (ma_device_get_state(pDevice) == ma_device_state_started),
function ma_device_stop, file miniaudio.h, line 44367.
```
So the assertion that would make this loud is compiled out of the build that ships.

TSan confirms the underlying race on the plain bool:
```
WARNING: ThreadSanitizer: data race
  Write of size 1 by thread T10:  MiniaudioCapture::start()  miniaudio_capture.cpp:61   // initialised_ = true
  Previous write of size 1 by T9: MiniaudioCapture::stop()   miniaudio_capture.cpp:75   // initialised_ = false
SUMMARY: ThreadSanitizer: data race miniaudio_capture.cpp:61
```

This is a **quit-path hang**, and BUG-42 already records quit taking up to 5 s — i.e. the shutdown sequence is already slow enough for a second stop to overlap.

---

## AUD-3 — SEV-1 — VERIFIED — the mutex on the audio callback starves the UI thread for **up to 9.9 seconds**

This is the direct attack on the audit's acceptance. `onData` holds `sinkMutex_` **across the entire user sink call** (miniaudio_capture.cpp:100-110). `std::mutex` is not fair on macOS, so once the sink's cost approaches the callback period the audio thread reacquires before the waiting UI thread ever gets it.

```
clang++ -std=c++20 -O2 -I"$SRC/src" -I"$SRC/third_party/miniaudio" h7_starve.cpp $AUD $FW -o h7
for ms in 1 5 15 19 21 30 60; do ./h7 $ms 15; done
```
```
sink=  1 ms | callback period= 9.95 ms | setSink() blocked the UI thread for     0.00 ms
sink=  5 ms | callback period=10.09 ms | setSink() blocked the UI thread for    14.87 ms
sink= 15 ms | callback period=19.46 ms | setSink() blocked the UI thread for   130.09 ms  <-- FROZEN
sink= 19 ms | callback period=24.27 ms | setSink() blocked the UI thread for  5059.77 ms  <-- FROZEN
sink= 21 ms | callback period=25.97 ms | setSink() blocked the UI thread for   392.28 ms  <-- FROZEN
sink= 30 ms | callback period=34.88 ms | setSink() blocked the UI thread for  5448.23 ms  <-- FROZEN
sink= 60 ms | callback period=65.74 ms | setSink() blocked the UI thread for  9899.98 ms  <-- FROZEN
```
Independently at 250 ms the block exceeded a 30 s watchdog entirely (`h5_rt.cpp d5`).

The threshold is not exotic: it is simply "the sink occasionally takes longer than one buffer period". Vosk's `accept_waveform` on a 20 ms chunk, a page fault, or a slower moment on battery all cross it. `setSink()` will be called from the Qt main thread — **the thread that services the keyboard, which the brief names as the guaranteed control path.**

Note the header's own contract at audio_capture.hpp:37-39 says the RT thread does "no allocation, **no locks**". The implementation takes a lock and holds it across arbitrary user code. The comment at miniaudio_capture.cpp:93-94 justifies it as "uncontended in steady state because the sink is set once before start()" — that is only true if `setSink()` is never called while running, which nothing enforces and which the API openly invites.

`stop()` is *not* affected the same way (it calls `ma_device_stop()` first, so the worker is already halted before it wants the lock — measured 0.13 ms).

---

## AUD-4 — SEV-2 — VERIFIED — a sink that calls back into the capture object deadlocks the audio thread permanently

Two independent variants, both hang past a 30 s watchdog (`h5_rt.cpp`):

```
./h5_plain d3   # sink calls stop()
[D3] the sink calls stop() from inside the audio callback
  D3a sink is calling stop() from the RT thread ...
  *** WATCHDOG: HUNG for 30 s at stage 'D3 stop() from inside callback' ***   # D3b never printed

./h5_plain d4   # sink calls setSink()
[D4] the sink calls setSink() from inside the audio callback
  D4a sink is calling setSink() from the RT thread ...
  *** WATCHDOG: HUNG for 30 s at stage 'D4 setSink() from inside callback' ***  # D4b never printed
```

- **D3**: `stop()` → `ma_device_stop()` waits for the audio thread to finish — *from the audio thread*. Calling `stop()` on device failure is the obvious thing a recogniser sink would do.
- **D4**: `onData` already holds `sinkMutex_`; `setSink()` relocks the same non-recursive `std::mutex` — undefined behaviour per `[thread.mutex.requirements.mutex]`, manifesting as a permanent self-deadlock.

Neither is documented as forbidden. The header's sink contract lists only "no allocation, no locks, no Qt objects, no logging".

---

## AUD-5 — SEV-2 — VERIFIED — an exception from the sink kills the app, and the module's own API forces the allocation that throws

```
./h5_plain d6
[D6] the sink throws an exception (e.g. bad_alloc from toRecognizerFormat)
  D6a throwing std::bad_alloc from the RT thread ...
libc++abi: terminating due to uncaught exception of type std::bad_alloc: std::bad_alloc
```
`onData` has no `try`/`catch` and is invoked from C frames inside miniaudio. Worse, the contradiction is built in: the header forbids allocation on the RT thread, but `toRecognizerFormat` — the only conversion helper offered — **returns `std::vector<int16_t>` by value**, so any sink using it *must* allocate per callback and *can* throw. Confirmed empirically (`./h5_plain d8`: 200 callbacks, 32,000 samples converted, one vector allocated per callback on the RT thread).

---

## AUD-6 — SEV-2 — VERIFIED — `start()` has a check-then-act race; concurrent `start()`, and `start()` racing `stop()`, corrupt the device and hang

`start()` reads `running_.load()` then unconditionally runs `ma_device_init(&device_)` — no test-and-set. Two threads re-initialise the *same* `ma_device`, clobbering the first device's thread handles and stop events.

```
clang++ -std=c++20 -O1 -I"$SRC/src" -I"$SRC/third_party/miniaudio" h6_start.cpp $AUD $FW -o h6_plain
./h6_plain e1   # 4 threads start() at once  -> *** WATCHDOG: HUNG at 'E1 stop after concurrent start' ***  (round 0)
./h6_plain e2   # start() vs stop()          -> *** WATCHDOG: HUNG at 'E2 round' ***
```
TSan (same sources, `-fsanitize=thread`) reports ~15 distinct races, all writes from `ma_device_init` at **miniaudio_capture.cpp:40** against reads from `ma_device_start`, `ma_device_stop`, and `onData` (`miniaudio_capture.cpp:96`, on miniaudio's worker thread) — i.e. the object is being re-initialised underneath a live callback.

Reachability caveat: there is no production caller yet (BUG-51), so this is latent — but `stop()`-vs-`start()` is the natural shape of a "toggle voice" control plus a device watchdog.

---

## AUD-7 — SEV-2 — VERIFIED (by absence) — device failure after start is never detected

`grep -rn "notificationCallback\|stopCallback\|onNotification" src/` → **nothing**. `cfg` at miniaudio_capture.cpp:25-38 sets no notification callback. `CaptureError::DeviceFailed`, whose comment reads *"it started and then stopped"*, can only ever be returned from the initial `ma_device_start` failure at line 57-59.

Unplug the USB mic or switch audio interfaces mid-talk and `isRunning()` keeps returning `true`, `running_` stays set, no message reaches the presenter, and no code path can report the one error the enum was written for.

---

## AUD-8 — SEV-3 — VERIFIED — undefined behaviour **inside** the BUG-56 guard

`src/audio/audio_format.cpp:94` — `std::vector<std::int16_t>(interleaved, interleaved + frameCount)`. For `channels == 1`, `needed == frameCount`, so the guard `needed > sampleCount` admits any `frameCount <= sampleCount`, including `SIZE_MAX`. Forming the pointer is UB before any read.

```
clang++ -std=c++20 -O1 -fsanitize=undefined -fno-sanitize-recover=undefined -I"$SRC/src" \
  h9_ub.cpp "$SRC/src/audio/audio_format.cpp" -o h9 && ./h9
```
```
calling toRecognizerFormat(buf, SIZE_MAX, SIZE_MAX, {16000, 1})
src/audio/audio_format.cpp:94:81: runtime error: addition of unsigned offset to
  0x00016d282d20 overflowed to 0x00016d282d1e
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior audio_format.cpp:94:81
exit=134
```
Gated on a caller that lies about `sampleCount`, so not reachable from `onData` today — but it is UB sitting inside the guard written to fix a memory-safety bug, which is the exact pattern that produced BUG-52.

---

## AUD-9 — SEV-3 — VERIFIED — the BUG-56 guard is **structurally unreachable** from the only call shape a sink can use

`CaptureSink` is `void(const int16_t* samples, size_t count)` (audio_capture.hpp:40). The sink gets **no `frameCount` and no `AudioFormat`**, so it must derive `frameCount = count / deviceFormat().channels`. Then `needed = (count/ch)*ch <= count == sampleCount` — **always**. The guard cannot fire.

```
./h1r    # h1_format.cpp, -fsanitize=address,undefined
A7a count=3840 staleFmt={48000,2} derivedFrames=1920 -> guard ACCEPTED (mismatch undetected), out=640
A7c swept count=1..8192 x channels=1..64: guard fires 0 times out of ~500k
```
And live, through the real object (`./h5_plain d8`): `calls=200 mismatches=0 rejected=0 converted=32000` — the guard never rejected anything in 200 real callbacks.

So the *memory-safety* half of BUG-56 survives (the derivation is self-consistent, so no over-read), but the *detection* half — "a device that changed shape mid-stream is caught" — is dead on the production path. A device that switches from 8ch to 2ch yields 640 samples of interleave-scrambled garbage instead of a rejection. Same shape as BUG-55.

**Credit where due:** I mutation-tested the guard. Removing lines 89-92 and rebuilding `tests/test_audio_format.cpp` against the mutant genuinely fails — `1 failed` plus an ASan `SIGABRT` on the "raises its channel count" subcase. That test is real, not a pass-with-the-fix-removed.

---

## AUD-10 — SEV-3 — VERIFIED — `CaptureError::NoDevice` is unreachable

`grep -rn "NoDevice" src/` returns only `audio_capture.hpp:32` (declaration) and `audio_capture.cpp:14` (its message). No code path returns it, and AUD-1 explains why it never could.

---

## SEV-4 / notes

- **`downmixToMono` is public with no buffer-length parameter.** The BUG-56 fix added `sampleCount` to `toRecognizerFormat` only. `downmixToMono(ptr, frameCount, channels)` still reads `frameCount * channels` samples with no way for a caller to state what exists, and no resource cap (`out.reserve(frameCount)` is unbounded).
- **Verified NOT a bug — do not "fix" it.** I suspected `format_` would go stale when the default input device changes mid-talk (AirPods). It does not: `ma_device__post_init_setup` (miniaudio.h:42495-42548) only fills `capture.channels`/`sampleRate` **when they are 0**, so after the first init miniaudio preserves the negotiated user format and inserts a converter. The snapshot stays correct.

**Survived attack (worth stating, so the good parts aren't touched):**
- `stop()` vs live callbacks — 100 rounds clean under TSan *and* ASan+UBSan.
- `setSink()` vs live callbacks — 43M swaps against 200 callbacks, clean under ASan+UBSan; 3.9M under TSan.
- Destructor vs live callbacks (single control thread) — 80 rounds clean.
- Rate/channel resource caps hold: every boundary rate (0/1/7999/8000/192000/192001/384000/INT_MAX/INT_MIN/negative) correctly refused; max amplification from a 4096-sample buffer is **24.0×** (8000→192000), bounded.
- 16 threads × 4000 concurrent conversions through `toRecognizerFormat`/`downmixToMono`/`resampleMono`: clean, no oversized output.
- 64-channel downmix at full positive and negative scale is exact (no int16 wrap).

**Known, not re-reported:** BUG-40 flake reproduced — `tests/pptv_tests` failed **1/10** runs in one batch and **3/5** in another, always `test_deck_load_worker.cpp:141 REQUIRE(spy.wait(5000))`. Higher than "flaky" implies.

**Harnesses:** `h1_format.cpp` (pure-function ASan+UBSan), `h2_capture.cpp` (API storm), `h3_null.cpp` (null-backend probe), `h4_backend.cpp` (backend identification), `h5_rt.cpp` (RT-thread scenarios D1-D8, watchdogged), `h6_start.cpp` (start races), `h7_starve.cpp` (starvation measurement), `h8_dblstop.cpp` (stop races), `h9_ub.cpp` (minimal UB repro), `mut/` (mutation test) — all in `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4/`.

---

**Highest-value fix order for a talk in 5 days:** AUD-1 (one `#define`, removes a silent total-failure mode on the presenter's machine) → AUD-3 + AUD-2 (do not call `setSink()`/`stop()` from the UI thread while running; make `initialised_` atomic and serialise `start`/`stop` under their own mutex) → AUD-7 (wire `notificationCallback` so `DeviceFailed` can actually fire) → AUD-9 (widen `CaptureSink` to carry frame count and format, or the mid-stream-change detection stays cosmetic).

---

**TL;DR (plain English)**

The microphone code has one really serious problem and several others behind it.

The big one: this computer has no microphone at all, and yet the app cheerfully reports "microphone started, everything's fine" and then quietly records **pure silence forever**. The audio library it uses has a built-in "pretend device" that it falls back on when no real microphone can be opened, and nobody turned that off. The worrying part is that the same fallback almost certainly kicks in on the presenter's laptop if he taps "Don't Allow" on the microphone permission popup — so instead of the helpful message the team wrote ("Microphone access is off, the keyboard still works"), he'd get a green light and total silence, with no clue why voice commands do nothing. Turning this off is a one-line change.

The second problem: if two parts of the program try to shut the microphone down at the same time — which is exactly what can happen when you quit — the app **freezes permanently**. It never finishes closing. In the developer's test build this would crash loudly and get noticed; in the version that actually ships, the loud warning is switched off, so it just hangs.

Third: there's a lock protecting the microphone data, and the audio hardware holds onto it the whole time it's processing each chunk of sound. If processing ever takes slightly longer than expected, the main part of the app — the part that listens for keypresses — gets stuck waiting. I measured freezes of up to **ten seconds**. Since the keyboard is meant to be the one thing that never fails during the talk, that's the most dangerous of the lot.

There are also a few ways to make the app lock up or crash outright depending on what the not-yet-written speech recogniser does, plus a safety check that was added to fix an earlier bug but which, in the way the code will actually be used, **can never trigger** — I tested half a million combinations and it fired zero times.

Some good news: the parts that were tested properly hold up well. The maths that converts audio to the right format is solid under heavy multi-threaded hammering, correctly rejects nonsense settings from a broken microphone, and one of the earlier bug fixes has a genuine test behind it — I deliberately broke the fix and the test caught it, which is exactly what you want.