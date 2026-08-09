# UAT Session 5 — human arm, part 3: false triggers (TM-002 / TM-019)

**Tester:** Karl Raulerson (human)
**Date:** 2026-08-06
**Machine:** MacBook Pro M3 Max

## Method

Reported verbatim: *"I reran the test using a video of people talking in the background on my phone
at a volume about 25% lower than my voice. All commands worked, no inadvertant slide changes during
the test."*

This is a stronger test than the one specified, which asked only for silence-from-the-deck while
speaking normally. Karl ran **competing continuous speech** at a realistic relative level and
exercised the commands **at the same time**, so it verifies both directions in one pass:

- **negative:** background conversation did not trigger any command
- **positive:** the presenter's commands still worked with that speech present

## Result: PASS

**TM-002 / TM-019 is now verified empirically on the target hardware.** This is the threat the entire
grammar design exists to prevent — the audience sits within earshot of the presenter's microphone —
and until now it was supported only by construction (a dynamic-graph model, a grammar-constrained
decoder, every grammar word verified in-vocabulary, and the matcher's two-word phrase rule) plus
synthesised-audio testing by agents.

BUG-64 is closed in full: (a) projector, (b) voice recognition, (c) false triggers.

## What it does not establish

Stated so the record is not read as more than it is:

- one room, one microphone, one background source at roughly -25% relative level
- not tested: speech at or above the presenter's level, someone addressing him directly, a large
  room's reverberation, or applause
- the residual risk is judged acceptable: the failure mode is a single unwanted slide change, the
  presenter can see it happen, and "previous slide" corrects it immediately

## Standing mitigation

Unchanged and untouched by any of this: the keyboard drives every command, no voice failure can stop
a talk, and "pause presentation" suspends voice entirely for Q&A — which is the moment the audience
is most likely to be speaking.
