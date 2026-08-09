# UAT Session 5 — human arm, part 1: projector

**Tester:** Karl Raulerson (human)
**Date:** 2026-08-05
**Machine:** MacBook Pro M3 Max with the presentation projector attached
**Build:** self-contained test bundle from `scripts/make-test-build.sh`

## Result

**PASS.** Reported verbatim: *"Project was tested. It's good."* — clarified on request to mean **the
projector specifically, not the project as a whole**.

## What this closes

**BUG-25 is now verified on hardware for the first time.** It was found by an agent reading code in
session 3, fixed by inspection, and never confirmed against a real second display. Its failure mode
was the deck covering 75% of the projector with 13% smaller text for the entire talk — invisible
unless a second screen of a different aspect ratio is attached, and therefore untestable on the
development Mac mini (RustDesk presents a virtual display) and by every agent.

This was the single highest talk-risk item outstanding.

## What this does NOT close

**Voice has never been run against a human voice.** Every test in the suite uses silence or a fake
decoder. Unproven: that the grammar holds against real speech in a room, that recognition latency is
usable for presenting, that the microphone permission flow behaves, and that an audience talking
nearby cannot trigger a command (TM-002/TM-019).

Tracked as BUG-64(b). Must clear before the live talk and before the Phase 2 → 3 gate.
