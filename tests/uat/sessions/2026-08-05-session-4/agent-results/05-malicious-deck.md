All work complete. Final integrity check passed — no project file was modified.

---

# UAT-4 · The automated suite itself: is it telling the truth?

**Verdict: no.** 33 behaviour-changing mutations to the newest production code, **0 killed**. Two positive controls, **2 killed** — so the harness works. And the flaky group is five tests in two files, not one, failing up to 30% of runs.

## Setup

Work done in a scratch copy; the project tree was never written to (`git status` clean, `diff -r src` identical). HEAD moved from `cd11cf8` to `89004de` mid-run, but that commit touched only `WALK-STATE.md` and `.claude/process-state.json` — `src/`, `tests/` and `CMakeLists.txt` are byte-identical to my snapshot.

```
cmake -S <scratch>/src-copy -B <scratch>/b1 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build <scratch>/b1
ctest --test-dir <scratch>/b1 --output-on-failure     # 224 tests
```

Harness: `<scratch>/mutate.sh` applies one mutation to pristine source, rebuilds, runs the **full** ctest suite, restores. Results in `<scratch>/mut-results{,3,4}.txt`, per-mutant ctest logs in `<scratch>/mut-out/`.

**Harness validation (VERIFIED).** Two mutations chosen to be caught:
```
c01 (start_view.cpp: accept remote URLs)  -> KILLED by  224 - S/BUG-18: only a LOCAL file is accepted from a drop
c02 (audio_format.hpp: channels <= 100000) -> KILLED by   19 - AF/audit F8a-1: an implausible device-reported format is REJECTED
```

Withdrawn as equivalent mutants after failing to find any input that changes behaviour: **m06** (over-crop guard `>=`→`>`; width is 0 either way and the downstream `src.width() <= 0.0` guard catches it) and **m18** (`x + 0.0`). Both survived; neither is a finding. That leaves **31 genuine surviving mutations**.

---

## PART A — Determinism

### F-16 · SEV-2 · Five non-deterministic tests in two files, up to 30% of runs red — BUG-40 is scoped too narrowly

**VERIFIED.** `bash <scratch>/flaky.sh` (26 full-suite runs) and `bash <scratch>/flaky-focus.sh` (60 runs of the 19 worker-thread tests, idle and under load on a 12-core M4).

| Campaign | Runs | Red | Rate |
|---|---|---|---|
| Full suite, 224 tests | 26 | 5 | **19.2%** |
| Worker group only, idle | 60 | 9 | **15.0%** |
| Worker group, 10 busy loops on 12 cores | 60 | 18 | **30.0%** |

Per-test failure counts:

| Test | File:line | idle/60 | load/60 |
|---|---|---|---|
| #42 `P: a result arriving after cancel is discarded` | test_deck_load_worker.cpp:118 | 5 | 13 |
| #43 `P: every failure kind survives the thread boundary` | test_deck_load_worker.cpp:141 | 3 | 6 |
| #44 `P: a worker cancelled before it starts does no work` | test_deck_load_worker.cpp:164 | 0 | 1 |
| #123 `O: cancelling before the run does no work` | test_pre_render_worker.cpp:223 | 1 | 0 |
| #124 `O: cancelling mid-run stops promptly` | test_pre_render_worker.cpp:240 | 0 | 1 |

**BUG-40 says "flaky worker-thread tests" and has been re-run past three times. It is two files, five tests, and a fifth of all clean-machine runs.** #123 and #124 live in `test_pre_render_worker.cpp`, which is not where anyone has been looking.

**Root cause — VERIFIED by isolated reproduction, not inferred.** All five fail on the same line shape: `REQUIRE(spy.wait(5000))`. Qt 6 documents `QSignalSpy::wait()` as *"starts an event loop that runs until the given signal is received or timeout has passed"* — it counts emissions received **during** the loop. A worker thread that emits before the main thread enters `wait()` is invisible to it. Every failing run took **exactly 5.0–5.4 s**, i.e. the full timeout, which only happens if nothing new ever arrives.

Direct proof, no project code involved (`<scratch>/spyrepro.cpp`):
```
iterations=200  wait()==true: 1   wait()==false WHILE count()>0: 199
```
199 times in 200, `wait()` reported failure while the spy already held the signal.

**Fix is one line per site:** `REQUIRE(spy.count() > 0 || spy.wait(5000));`
Sites: `test_deck_load_worker.cpp:73,97,118,141,164` · `test_pre_render_worker.cpp:117,146,177,200,223,240,256,291`.

---

## PART B — Surviving mutations

### F-1 · SEV-1 · The suite cannot tell whether the app can open a deck at all

Three separate mutations, each leaving all 224 tests green:

| id | mutation | file |
|---|---|---|
| m22 | delete the `QShortcut(QKeySequence::Open, …)` — Cmd+O gone | `src/ui/start_view.cpp` |
| m23 | `dropEvent` no longer emits `fileDropped` — drag-and-drop dead | `src/ui/start_view.cpp` |
| m24 | delete both `connect(start_, …)` lines — button and drop wired to nothing | `src/ui/app_shell.cpp` |

Applied together these restore **exactly the BUG-18 state**: button does nothing, Cmd+O does nothing, drop does nothing, deck only openable from argv. `tests/ui/test_widgets.cpp:336` says *"Nothing in 211 tests noticed, because nothing asserted that the application is USABLE."* That sentence is still true. `S/BUG-18: the start screen offers a control that asks to open a deck` asserts the button **emits a signal**; nothing asserts anyone is **listening**. `src/ui/app_shell.cpp` — every wire in the application — has zero tests.

Severity: BUG-18 shipped, was caught only by Karl on a real machine, and five days from the talk the suite still cannot catch its return.

### F-2 · SEV-2 · JPEG support is untested; deleting it entirely leaves the suite green

`m07` — `isAllowedImageFormat` no longer recognises the JPEG SOI marker. Suite green. Probe (`<scratch>/probe.cpp`, pristine vs mutant):

```
PRISTINE   PROBE1 jpeg: red=51809 greyPlaceholder=0
MUTANT m07 PROBE1 jpeg: red=0     greyPlaceholder=47313
           PROBE1 png : red=51809 greyPlaceholder=0     <- unchanged, so no test notices
```

Every image fixture in `tests/fixtures/` is a PNG — verified by `unzip -l` across all 36. The reference deck's media parts, by extension only: **5 emf, 2 jpeg, 5 png, 1 wdp**. Two real photographs would become grey "missing image" boxes on the projector with a fully green suite.

### F-3 · SEV-2 · The srcRect out-of-bounds clamps have zero coverage — and the live deck depends on them

`m10` (drop `.intersected(whole)`) and `m11` (drop the `qMin` clamps on the 1-pixel widening) both survive, each confirmed over two clean runs. These are the guards written to stop Qt receiving a source rect outside the image.

```
PRISTINE   PROBE6 negative inset r=-6769 b=41679:      red=51809
MUTANT m10 PROBE6 negative inset r=-6769 b=41679:      red=48461
MUTANT m10 PROBE6 negative inset l=-6726 r=-1 b=36909: red=48461
PRISTINE   PROBE5 sub-pixel crop at right edge: red=51809
MUTANT m11 PROBE5 sub-pixel crop at right edge: red=33095
```

Those inset values are not invented — they are read verbatim from the reference deck. Structural scan of its 12 `<a:srcRect>` elements: **6 carry negative insets** (`r="-6769"`, `l="-6726" r="-1"`, `t="-190"`). This is also the live surface for the open BUG-54.

### F-4 · SEV-2 · `audio_format.cpp` has 12 test cases and not one constrains the arithmetic

All five mutations survived.

- **m01** `sum / channels` → `sum / 2`. A three-channel buffer at 24000 per channel: pristine `24000`, mutant **`-29536`** — a full-scale polarity flip, i.e. the loud click that the adjacent test *"AF: downmix does not overflow on loud input"* exists to prevent. It does not catch it because **every** downmix test in the file uses 2 channels. The file's own header states a MacBook Pro's microphone *"is a three-element array"*.
  ```
  PRISTINE   PROBE3 downmix 3ch: [0]=24000  [1]=200
  MUTANT m01 PROBE3 downmix 3ch: [0]=-29536 [1]=300
  ```
- **m02** drop the `+ inRate/2` round-half-up. 1000 frames @44.1 kHz: pristine 363 samples, mutant **362** — one sample dropped per callback, forever. The comment above that line says it exists so the tail *"does not silently drop… over a talk that adds up."* The two resampler length tests both use exact ratios (48000→16000, 44100 frames→16000) where rounding cannot change the answer.
- **m03** remove the `channels > kMaxChannels` resource cap in `downmixToMono`.
- **m04** replace linear interpolation with nearest-sample truncation.
- **m05** tail-hold emits silence instead of the last sample.

### F-5 · SEV-2 · Four decode guards from the R2/R3 audit are untested

- **m08** `setAllocationLimit(kMaxImageAllocMiB)` → `setAllocationLimit(0)`. Qt 6 documents 0 as *"the allocation size check will be disabled"* — the 128 MiB cap on attacker bytes is gone. Green.
- **m09** delete the 40 Mpx dimension guard. Green.
- **m32** delete the second-stage `QImageReader::format()` check. Green.
- **m33** reduce the PNG magic check from 8 bytes to 4. Green.

### F-6 · SEV-2 · The quit prompt can advertise a key that does not quit, and the test written to prevent that passes

`m27` — `quitConfirmChord()` returns `"Cmd+W"`. Suite green. Test #222 is titled *"the on-screen quit hint names a chord this platform actually delivers"*, but asserts only that the hint **contains "Cmd"**, **lacks "Ctrl"**, **lacks "Shift"** — never the key. It then drives the translator with a hard-coded `Qt::Key_Q` instead of the key the hint names, so the string and the behaviour are never connected. A presenter reading the projected instruction would press Cmd+W and nothing would happen.

### F-7 · SEV-2 · A close request silently ceasing to raise the quit prompt goes unnoticed

`m26` — `closeEvent` still ignores the event but no longer calls `uiSink_`. Survived two clean runs. BUG-43's stated remedy is *"a close request now RAISES THE QUIT PROMPT… and the user gets a visible way out"*; the only test of that path never installs a UI-request sink, so the window silently swallowing every close — the original UAT-3 force-quit failure — is unobservable.

### SEV-3 group

| id | mutation | note |
|---|---|---|
| m21 | `kMaxGroupDepth` 32 → **3200** | the cap that stopped a 5.7 KB deck killing the load worker; `deep_nest.pptx` doesn't nest deep enough to notice |
| m14 | `placeholderKey` drops the BUG-58 holder namespace | reference deck has **0** placeholder-key collisions, so no live impact today — but no regression guard either |
| m19 | drop the `type.isEmpty() → "body"` default | |
| m20 | stop searching `nvGraphicFramePr` / `nvCxnSpPr` | |
| m15 | `<a:stretch>` by descendant search, not direct child | reference deck: all **51** blipFills carry it as a direct child — no live impact |
| m16 | `<a:blipFill>` by descendant search, not direct child | |
| m29 | `alphaModFix` scope widened from `<a:blip>` to the whole `<p:pic>` | deck uses amt 20000/26000/70000 |
| m12 | R1 font clamp ceiling `renderedH` → `renderedH * 1000` | |
| m17 | `alphaModFix` clamp `qBound(0,·,100000)` → `100000000` | |
| m30 | bullet/list indent zeroed at render time | BUG-7 is asserted at the loader only |
| m31 | no-geometry text box falls back to the whole slide, not the 8%/10% content area | |
| m13 | placeholder box loses its diagonal | |
| m25 | quit-filter install-once guard removed (N filters, N× `closeAllWindows`) | |
| m28 | `PermissionDenied` returns the `NoDevice` message | the three AC tests check only: non-empty, contains "keyboard", <160 chars, no path/errno/0x — nothing ties an error to its meaning, so the presenter can be told "No microphone was found" when the real cause is a permission toggle they could fix in ten seconds |

### Structural gaps (not mutations — stated for completeness)

- `src/ui/app_shell.cpp` (~330 lines, all the application wiring) — **zero tests**.
- `src/audio/miniaudio_capture.cpp` (128 lines, the only real-hardware path) — `makeMiniaudioCapture()` has **no references outside its own header/definition**. Within known BUG-51, but the test consequence is that nothing in it can ever be exercised.

---

## Files

- Mutation results: `<scratch>/mut-results.txt`, `mut-results3.txt`, `mut-results4.txt`
- Per-mutant ctest logs: `<scratch>/mut-out/`
- Mutation scripts: `<scratch>/muts/m01.py` … `m33.py`, `c01.py`, `c02.py`
- Harness: `<scratch>/mutate.sh`, `run-muts.sh`, `run-muts3.sh`, `run-muts4.sh`
- Determinism: `<scratch>/flaky.sh` + `flaky-out/`, `flaky-focus.sh` + `focus-load0/`, `focus-load10/`
- Evidence probes: `<scratch>/probe.cpp` (+ `probe-run.sh`), `<scratch>/spyrepro.cpp`, `<scratch>/struct_probe.py`

(`<scratch>` = `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4`)

---

**TL;DR (plain English).** I deliberately broke 33 things in the app's newest code, one at a time, and ran the full 224-test suite after each. **The tests failed to notice a single one.** To prove the tests weren't just switched off, I broke two more things they *were* supposed to catch — they caught both immediately. The worst misses: the app can be made completely unable to open a presentation and the tests stay green; photographs saved as JPEG (the presenter's deck has two) can be made to vanish into grey boxes and the tests stay green; and the on-screen "press this key to quit" message can be made to name the wrong key while the test whose whole job is checking that message still passes. Separately, the tests are unreliable: on a quiet machine one run in five fails for no reason, and on a busy machine almost one in three — caused by five tests (not the one everybody knows about, and in two different files) that all wait for a signal in a way that misses it if it arrives a fraction of a second early. That last one is a one-line fix in each of about thirteen places.