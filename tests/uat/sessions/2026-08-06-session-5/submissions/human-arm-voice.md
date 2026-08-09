# UAT Session 5 — human arm, part 2: voice

**Tester:** Karl Raulerson (human)
**Date:** 2026-08-06
**Machine:** MacBook Pro M3 Max (the machine the talk runs on)
**Build:** self-contained bundle with libvosk and the speech model staged inside it

## Result: PASS on recognition

Reported verbatim: *"Voice worked great. Next slide, previous slide, pause presentation, continue
presentation, go to slide X, all worked with no issues."*

**This is the first time the voice path has been run against a human voice.** Every automated test
uses silence or a fake decoder. It closes BUG-64(b) for the POSITIVE direction: the five commands are
recognised, on the target hardware, by the person who will present.

Also confirmed implicitly: the microphone permission flow works, the model and library resolve from
inside the bundle, and the pause gate behaves — "pause presentation" then "continue presentation"
both took effect.

## NOT tested: the negative direction (TM-002 / TM-019)

Karl confirmed, on being asked specifically, that he did **not** run the false-trigger check:
speaking normally near the microphone for ~60 seconds without uttering a command, and confirming
nothing moves.

**That is the threat this feature's entire design exists to prevent.** The audience sits within
earshot of the presenter's microphone. The controls are layered — a dynamic-graph model, a
grammar-constrained decoder, every grammar word verified in-vocabulary, and a two-word phrase rule
in the matcher — and the agent arm is attacking the property with synthesised audio. But **no human
has confirmed it against real speech in a room.**

Consequence if it fails at the talk: the audience moves the presenter's slides. That is the
embarrassing failure rather than the inconvenient one.

**Recommended: 60 seconds before the talk.** Read anything aloud near the microphone with the deck
open and confirm the slide does not change.

Tracked as BUG-64(c).
