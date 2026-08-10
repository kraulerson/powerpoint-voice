# Pre-talk check — RESULT

**Tester:** Karl Raulerson (human arm)
**Date:** 2026-08-10
**Hardware:** MacBook Pro M3 Max — the machine the talk runs on, not the dev Mac mini
**Build:** `powerpoint-voice-test-build.zip`, from `main` @ 8180558
**Script:** `tests/uat/2026-08-10-pre-talk-check.md`

## Result: ALL FIVE CHECKS PASSED

Karl's words: *"Tests passed."*

| # | Check | Result |
|---|---|---|
| 1 | **P key** — tap pauses and says so, "next slide" ignored while paused, → still works, tap resumes, **HOLD for 2 s ends up PAUSED** | PASS |
| 2 | Voice — all five commands | PASS |
| 3 | Natural phrasing ("okay, next slide", "next slide please", "let's go to slide five") fires; the full sentence "the next slide shows our results for this quarter" does NOT | PASS |
| 4 | Slide 1's photograph present, full height; rest of the deck unchanged | PASS |
| 5 | ⌘Q and Dock → Quit after voice has been running | PASS |

## Why this run mattered more than the others

Four of these five could not have been verified anywhere else.

- **Check 1 closes BUG-81, a SEV-1 I introduced myself** in the fix for BUG-73. Holding P
  toggled the microphone gate once per key auto-repeat and landed back on **live**. An
  adversarial reviewer measured it against the real sink — *"after ONE held press of P the
  gate is ACTIVE (voice still live!)"* — and this is its first confirmation on hardware, by
  the person who would have been standing in front of the room.
- **Check 5 is the first possible test of BUG-72/79 ever.** `~AppShell` was freeing the
  VoskEngine while the CoreAudio thread was inside `feed()`. The development machine has no
  input device, so the code path could not execute there at all. Reproduced 7/7 under
  AddressSanitizer on a harness; this is the first time the real path ran on a real
  microphone and exited cleanly.
- **Check 3 closes BUG-71**, where my `[unk]` fix had been rejecting 36 of 60 naturally
  phrased commands — and confirms the counterexample that constrained the fix still holds:
  a full narrated sentence containing the command words does not move the deck (TM-002/019).
- **Check 4 closes BUG-70**, slide 1's photograph, which my own BUG-58 fix had made vanish
  for the second time.

## Coverage this does NOT give

Stated so the record does not overclaim:

- One tester, one deck, one room, one session. Not a statistical result.
- The near-miss false-trigger rate is unchanged and remains as measured: **60 of 102**
  isolated fragments, **0 of 6** natural sentences. This run did not re-measure it.
- Not tested: a second display / projector (verified separately on 2026-08-05, BUG-64),
  a deck other than Karl's, or a session longer than the check itself.
