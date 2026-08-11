# Security Policy

## What this application is

`powerpoint-voice` is a **fully offline** desktop presentation controller. It opens a `.pptx`, renders
it, and responds to five spoken commands. It runs on the presenter's own machine during a talk.

Three properties define its security posture, and each is enforced structurally rather than by
convention:

| Property | How it is enforced |
|---|---|
| **No network** | There is no network code. The drag-and-drop handler explicitly refuses remote URLs (`localDeckDropPathFrom`, `src/ui/start_view.cpp`) so a dropped file cannot become the first network path by accident. |
| **Nothing written to disk at run time** | No file is opened for writing anywhere in `src/`. The speech model is extracted at **build** time (`cmake/VendorVosk.cmake`), never on first launch. |
| **Nothing said in the room leaves the process** | Recognised text passes from the decoder into the command gate and nowhere else — never stored in a member, logged, or rendered. Vosk's own stderr logging is silenced before any model loads. |

## Reporting a vulnerability

Open a GitHub issue at <https://github.com/kraulerson/powerpoint-voice/issues>, or contact the
maintainer directly for anything you would rather not file publicly. There is no bug bounty.

This is a single-maintainer project. Expect acknowledgement within a few days rather than hours.

## What is in scope

The application processes **two untrusted inputs**, and both are treated as hostile:

### 1. The `.pptx` file

A presentation file is an arbitrary ZIP of arbitrary XML from an arbitrary source. The loader
assumes it is malicious:

- decompression caps are enforced from the **central directory before any part is read**, and sizes
  are compared unsigned so a ZIP64 declared size cannot wrap negative (TM-014/017)
- XML descent is **iterative, not recursive** — a deeply-nested hostile file cannot exhaust the call
  stack. Group nesting is separately capped at 32 (a 5.7 KB file previously killed the process)
- media parts are read **once and shared**; an earlier version re-read per reference, with a measured
  ~640 GB allocation ceiling from a ≤200 MB file
- image bytes are checked against a **PNG/JPEG magic-byte allow-list before any decoder is
  constructed**, so an unrecognised format never reaches a Qt image plugin
- four render caps (shapes, text runs, characters, declared image pixels) reject a pathological slide
  **before it is painted**, rather than trying to abort a paint in flight (TM-018)
- the raster window is bounded at 2 GB

Error messages are drawn from a **closed vocabulary** and never include a path or bytes from the
file — an earlier version printed freed heap into a projector-facing dialog.

### 2. The microphone

Everyone in the room can drive this input. The decoder is grammar-constrained, and the constraint is
verified rather than assumed: the model must carry a **dynamic graph** (`graph/HCLr.fst` +
`graph/Gr.fst`) or the application refuses to enable voice, because a static-graph model accepts a
grammar and then silently decodes the full ~200,000-word vocabulary.

**Measured, honestly:** the constraint bounds the **vocabulary**, not the word order. Vosk compiles a
grammar into a backoff bigram, not a phrase matcher. Across a 102-clip corpus of near-miss phrases,
**60 still matched a command**; all six full natural sentences matched none, because the matcher
requires the whole utterance. See
`tests/uat/sessions/2026-08-06-session-5/submissions/false-trigger-measurement.md`.

An earlier version of the changelog claimed the decoder was "incapable of emitting anything but the
five commands". **That claim was false and has been withdrawn.**

## What is out of scope

- **Multi-user or network attackers.** There is no server, no auth, no listening socket.
- **The confidentiality of the deck against someone with the machine.** The file is the user's own and
  is read from wherever they point the app.
- **Code signing and notarisation.** Test builds are ad-hoc signed. Distribution signing is Phase 4
  work; until then macOS Gatekeeper will require an explicit right-click → Open.

## Privacy

- Recognition is **entirely on-device**. No audio, transcript, or deck content is transmitted.
- No audio is recorded to disk.
- The macOS microphone consent string states exactly this, because that string is what the user is
  actually shown.

## Verifying a build

Dependencies are vendored and pinned by SHA-256 in `third_party/PROVENANCE.md`; `sbom.json` carries
the same hashes, independently re-verified against the committed files at generation time.

```sh
bash scripts/make-test-build.sh          # fails if the bundle is not self-contained
codesign --verify --deep --strict powerpoint_voice.app
```

## Known accepted risks

Tracked in `BUGS.md` and attested at the Phase 2→3 gate (`APPROVAL_LOG.md`, 2026-08-09):

- **Isolated near-miss speech can match a command** (measured 60/102 fragments; 0/6 natural
  sentences). Mitigations: "pause presentation" suspends voice **navigation** for discussion, and the
  keyboard drives every command independently of the speech engine.
- **Pausing does not stop the microphone, and the un-pause phrase is the weakest one** (BUG-88).
  While paused the gate keeps listening — it must, to hear "continue presentation" — and ignores
  every command except that one. That one is the corpus's worst performer: *"continue presenting"*,
  *"consume the presentation"*, *"presume the presentation"* and *"resume presenting"* each fired for
  **2-3 of 3 voices**. So an audience phrase can end a pause, restoring full voice control without
  the presenter noticing. Karl was offered a double-confirm mitigation on 2026-08-06 and chose to
  leave it (recorded in `false-trigger-measurement.md`); what was wrong until 2026-08-10 was the
  documentation, which told the presenter pausing "removes the risk entirely".
- **A logout or restart event quits without confirmation** (BUG-44) — correct for a real shutdown, a
  hazard if a system prompt fires mid-talk.
- Four further SEV-3 items: BUG-33, 39, 54, 55.
