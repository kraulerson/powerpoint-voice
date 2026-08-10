# Pre-Launch Preparation — Phase 3.8

**Date:** 2026-08-10
**Commit:** 63fa398
**"Launch" here means one thing:** Karl presents from this application on Wednesday
2026-08-12, in front of ~20 people, from a MacBook Pro M3 Max. There is no store, no
installer, no user base. Pre-launch preparation is whatever makes that talk survivable.

**Note:** `process-checklist.sh` accepted this step with **no artifact check** (as it did for
`contract_testing`). This record exists so the mark is not synthetic. See ISSUE-032.

---

## Delivered to the presenter

| Item | Where | State |
|---|---|---|
| The application | `~/Desktop/powerpoint-voice-test-build.zip` (80 MB, arm64, macOS 26+) | Delivered 2026-08-10 |
| Test script | `~/Desktop/PRE-TALK-CHECK.md`, tracked at `tests/uat/2026-08-10-pre-talk-check.md` | Delivered 2026-08-10 |
| Result | `docs/test-results/2026-08-10_pre-talk-check_pass.md` | **All five checks PASSED on target hardware** |
| User guide | `USER_GUIDE.md` | Current — includes the P key and the screen-reader section |
| Security posture | `SECURITY.md` | Current |
| Licence notices | `THIRD_PARTY_NOTICES.md`, and inside the bundle | Added 2026-08-10 |

## Bundle verification, on the artifact that was actually shipped

| Check | Result |
|---|---|
| References to `/opt/homebrew` | **0** — runs on a machine without Homebrew |
| `NSMicrophoneUsageDescription` present | Yes — without it macOS terminates the process outright (BUG-45) |
| Speech model staged, with a **dynamic graph** | Yes — `graph/HCLr.fst` + `graph/Gr.fst`; without both, the grammar is silently ignored and the decoder opens to ~200,000 words (BUG-66) |
| `libvosk.dylib` staged with `@rpath` install name | Yes (BUG-49) |
| Licence texts staged | Yes — build FAILS without them |
| Signature | `codesign --verify --deep --strict` passes (ad-hoc) |
| Architecture / minimum OS | arm64 / macOS 26.0 |

Every one of those is enforced by `scripts/make-test-build.sh`, which exits non-zero rather
than producing a bundle that runs here and nowhere else.

## The failure plan, which is the part that actually matters

**The keyboard is the guaranteed control path and cannot be gated.** If anything at all goes
wrong with voice — denied permission, missing model, a wedged device, a heckler — the talk
continues on the arrow keys. This is structural, not a fallback that was added: no capture
failure can stop the deck, and `B: while paused, the KEYBOARD still drives the deck` pins it.

| If this happens | Do this |
|---|---|
| Voice does not respond | Arrow keys. Check the on-screen message |
| An audience phrase moves the deck | "previous slide", or ← |
| Taking questions | **P**, or "pause presentation" — confirm "Paused — voice control is off" is on screen |
| Deck on the wrong screen | **⌃⇧D** |
| Something on screen shouldn't be | **Esc** blanks the projector |
| The app misbehaves entirely | **⌘Q**, relaunch, reopen the deck. Pre-render is ~300 ms |

## Known limits the presenter has been told about

- Isolated near-miss phrases can trigger a command: **60 of 102** fragments, **0 of 6** natural
  sentences. Pausing during discussion removes it.
- EMF/WMF images draw as a grey box with a cross; a converted `-png` deck avoids it.
- Tables, charts and SmartArt draw as labelled placeholder boxes.
- Animations and transitions are not run.

## What is deliberately NOT ready, and does not need to be

Distribution signing and notarisation, auto-update, an installer, checksummed downloads,
crash reporting, telemetry. All Phase 4, none of them on the path between Karl and Wednesday.
