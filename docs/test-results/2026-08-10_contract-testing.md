# Contract Testing — Phase 3.6

**Date:** 2026-08-10
**Commit:** 63fa398

**Note on how this record came to exist:** `process-checklist.sh` accepted
`--complete-step phase3_validation:contract_testing` **with no artifact check at all** —
unlike `security_hardening` and `accessibility_audit`, which both refused until their
evidence existed. Marking a step with nothing behind it is precisely the synthetic completion
this project's rules forbid, so this document was written immediately afterwards to make the
mark truthful. The framework gap is filed as ISSUE-032.

---

## What "contract" means for this application

There is **no external API, no service boundary, no network protocol, and no consumer outside
this process.** A conventional contract-test suite (provider/consumer, Pact, schema
compatibility) has nothing to bind to here, and pretending otherwise would be theatre.

What this application does have is **five internal boundaries whose contracts are written
down, are relied on across a thread, and have broken in practice.** Every SEV-1 and SEV-2 of
this project has lived at one of them. Those are what is tested.

---

## Boundary 1 — `IAudioCapture` (audio device → pipeline)

**Contract** (`src/audio/audio_capture.hpp`): the implementation reports the format it
actually got rather than the one requested; it hands over the sample count explicitly; every
failure is recoverable and carries an operator message; `stop()` joins any in-flight sink call
before returning.

| Clause | Test | Learned from |
|---|---|---|
| Format is reported, never assumed | `AC: the device's OWN format is used, never an assumed one` | BUG-46 — assuming 16 kHz would have decoded 48 kHz audio as plausible words |
| Buffer length is explicit and validated | `AF/BUG-56: a buffer smaller than the format claims is REFUSED, not over-read` | BUG-56, a real heap over-read confirmed under ASan |
| Every failure is recoverable | `AC: permission denied is a NORMAL state, not a crash and not a stop` | The keyboard must survive any microphone failure |
| No failure message leaks a device name or path | `AC: no failure message leaks a device name, a path, or an OS error string` | TM-013 |
| `stop()` joins the callback | `VP/F-1: stop() does not return while the decoder is still running` | **BUG-79** — the previous implementation did not, and the comment claiming it did was wrong |

## Boundary 2 — `IRecognizer` / `RecognizerController` (speech → command)

**Contract** (`src/command/recognizer_controller.hpp`): only finalised phrases, one call per
utterance; the caller marshals onto the controller's thread; the sink runs synchronously
inside `onPhrase`; an exception from the sink is swallowed rather than crossing the audio
thread; state is committed **before** the sink is called; at most one Command per call.

| Clause | Test |
|---|---|
| At most one command per phrase | `active: a nav phrase dispatches exactly one command` |
| Paused gates navigation, never the keyboard | `B: while paused, a VOICE navigation command is suppressed` / `B: while paused, the KEYBOARD still drives the deck` |
| A throwing sink never escapes | `audit S3: a throwing sink never propagates out of onPhrase` |
| Reentrancy is dropped | `audit S2: a reentrant sink is dropped, dispatching once` |
| State is committed before the sink | `F-2: setPaused is refused from inside that controller's OWN dispatch` |
| Non-speech callers reach the same state | `F-2: a keyboard pause and a spoken pause agree, in both directions` |

## Boundary 3 — `DeckLoadWorker` (untrusted file → GUI thread)

**Contract** (`src/present/deck_load_worker.hpp`): the outcome crosses as a registered
metatype; the deck crosses as a `shared_ptr`, never a deep copy; a result arriving after
`cancel()` is discarded; the error kind survives typed; nothing escapes the thread.

| Clause | Test |
|---|---|
| Metatypes registered, or the signal silently vanishes | `P: the metatypes needed to cross the thread boundary are registered` |
| Pointer, not a deep copy | `P: the deck crosses to the GUI thread as a pointer, not a deep copy` |
| Late results are dropped | `P: a result arriving after cancel is discarded, never delivered late` |
| Every failure kind survives typed | `P: every failure kind survives the thread boundary, typed` |
| **Nothing escapes the thread** | `P/F-CHAOS-3: an exception in the parse becomes a load FAILURE, not a crash` |

## Boundary 4 — `PreRenderWorker` (render thread → GUI thread)

**Contract** (`src/present/pre_render_worker.hpp`): QImage crosses, never QPixmap; each slide
is rendered exactly once; caps PREVENT rather than abort; a null raster is never emitted;
`finished()` is emitted on every path.

| Clause | Test |
|---|---|
| QImage, never QPixmap (Qt: QPixmap is GUI-thread only) | `O: the ready signal carries QImage, never QPixmap` |
| Each slide exactly once, right index | `O: every slide is rendered exactly once, with the right index` |
| Caps prevent, not abort | `O: THE RENDER BOMB IS NEVER PAINTED — the cap prevents, it does not abort` |
| A null raster is never emitted | `R/F-CHAOS-3: a throwing PLACEHOLDER still yields a raster, never a null` |
| One bad slide costs one slide | `R/F-CHAOS-3: a throwing slide becomes a placeholder, the rest still render` |

## Boundary 5 — `AppShell` wiring (the one with no contract, historically)

This is where every Critical in the F7b audit lived, and where BUG-60 proved three mutations
could restore "the app cannot open a deck" with the whole suite green. It now has explicit
tests rather than a written contract:

| Property | Test |
|---|---|
| The start screen is actually wired to opening a deck | `S/BUG-60` (4 subcases, all three mutations killed) |
| The P key reaches the voice gate | `W/F-2: the P key actually pauses the voice gate, and toggles back` |
| The engine is not freed under a live decode | `W/F-1: the shell never frees the speech engine under a live decode` |
| A stale worker's raster is dropped | `W/F-CHAOS-2` (guard) + `W/F-CHAOS-2: the REAL openDeck path bumps the generation every time` (production path) |
| Spoken state reaches the accessibility tree | `W/A11Y-1: the shell actually WIRES the spoken state to the presentation` |

---

## Result

**All five boundaries have executable contract tests. 297 tests green, all of which run**
(verified by `scripts/lint-test-names.sh` — see BUG-76 for why that verification exists).

## What this does NOT cover

- **No consumer-driven contract testing**, because there is no consumer. If this application
  ever gains a plugin API, an export format, or a companion app, that changes.
- **The OOXML "contract" with PowerPoint is not tested against PowerPoint.** Fixtures are
  synthetic and hand-built; the only real-deck verification is Karl looking at the screen.
  Every deck-fidelity bug in this project (BUG-32/37/41/47/53/57/70) was found that way, not
  by a test — which is the honest measure of how much this boundary is really covered.
