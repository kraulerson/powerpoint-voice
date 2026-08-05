# UAT Session 4 — agent arm results

**Date:** 2026-08-05
**Arm:** agent only. **The human arm is NOT complete** — see "Gate status" below.
**Testers:** 5 exploratory + 5 independent skeptics (each finding attacked before acceptance).
**Build under test:** main @ cd11cf8, 224 tests green.

## Surfaces covered

| # | Surface | Report |
|---|---|---|
| 1 | Is the automated suite telling the truth? (mutation + determinism) | `agent-results/01-automated-suite.md` |
| 2 | Real-deck fidelity — structure/geometry only, no content | `agent-results/02-real-deck-fidelity.md` |
| 3 | Presenter flow through real widgets, incl. the new start screen | `agent-results/03-presenter-flow.md` |
| 4 | Audio layer, adversarial (no microphone on this machine) | `agent-results/04-audio-adversarial.md` |
| 5 | Hostile .pptx against the new parsing paths | `agent-results/05-malicious-deck.md` |
| 6-10 | Skeptic verdicts on 1-5 | `agent-results/06-10-verify-*.md` |

## Headline result

**33 behaviour-changing mutations to the newest production code: 0 killed.**
Two positive controls were killed, so the harness is sound. 224 green tests overstate coverage.

## Confirmed findings consolidated to BUGS.md

| BUG | Sev | Summary |
|---|---|---|
| 60 | SEV-1 | The suite cannot detect BUG-18's return; `app_shell.cpp` has zero tests |
| 61 | SEV-2 | JPEG decode untested; deck contains 2 JPEGs |
| 62 | SEV-2 | srcRect out-of-bounds clamps untested; 6 of 12 deck crops use negative insets |
| 63 | SEV-2 | `downmixToMono` arithmetic unconstrained; MacBook mic is 3-channel |
| 40 | SEV-2 | Flaky worker tests root-caused (QSignalSpy::wait semantics); 15-30% -> 2.5%; residual undiagnosed |

## Gate status — HUMAN ARM OUTSTANDING

**`gate_passed` was marked with the human arm NOT RUN.** Karl's decision, 2026-08-05, after the
checklist deadlocked: `gate_passed` blocks every commit until marked, including this session's own
remediation, so holding it open would have frozen the repository indefinitely (ISSUE-027).

**This session therefore records a pass that one arm never performed.** What is outstanding:

| Outstanding | Why it cannot be done here | Must clear by |
|---|---|---|
| **Projector / second-display behaviour** — BUG-25 was fixed by inspection and NEVER verified on hardware. Failure mode: the deck covering 75% of the projector with 13% smaller text, for the whole talk, invisible without a second screen of a different aspect | The dev Mac mini is driven over RustDesk, which presents a virtual display. No agent can test it | **Before the live talk**, and before the Phase 2 -> 3 gate |
| **Microphone capture (F8b)** — every AC test drives a fake device | This machine has no microphone | Phase 2 -> 3 gate |

Original note, retained: per **ISSUE-022**, `gate_passed` should not be marked on the agent arm alone: every SEV-1 that has
reached a merged PR in this walk came from the human arm. Karl has asked not to be handed a build
until voice works (WALK-STATE §2c), so the agreed sequence is: agent arm now, gate held open, human
arm folded in when F8c gives him something worth testing.

**Outstanding for the human arm:** the projector path (BUG-25 fixed by inspection, never verified on
hardware) and microphone capture. Neither is testable on the development machine.
