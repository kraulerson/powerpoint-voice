# UAT-5 — measured false-trigger rate

**Date:** 2026-08-09
**Method:** 34 phrases x 3 macOS voices (Daniel, Fred, Karen) synthesised with `say` at
16 kHz mono LEI16, fed through the REAL `VoskEngine` with the REAL grammar in 3200-sample
chunks exactly as `VoicePipeline` does, then each final transcript run through `matchCommand()`.
Harness: scratch only, not committed.

## Numbers

| Build | Near-miss clips firing a command | True commands firing |
|---|---|---|
| `[unk]` REMOVED (baseline) | **75 / 102** | 13 / 15 |
| `[unk]` present (shipped) | **60 / 102** | 13 / 15 |

The baseline was measured on THIS corpus rather than taken from the UAT-5 tester's report, so the
comparison is like-for-like. Their corpus gave 98/102 and 85/102 for the same two conditions; the
absolute numbers differ with the phrase set, the direction and magnitude agree.

**`[unk]` reduces false triggers by ~20% and costs nothing in true-command recognition.**
It does NOT deliver the property originally claimed. BUG-66's withdrawal of that claim stands.

## The distinction that decides the risk

**All six natural sentences fired ZERO times across all three voices:**

- "the next flight was delayed" · "we should pause and discuss"
- "let me continue with the next point" · "that concludes the presentation"
- "any questions about the previous quarter" · "our revenue grew nine percent"

Every remaining failure is a near-miss phrase uttered ALONE with silence either side
("next flight", "applause presentation", "presume the presentation"). `matchCommand()` requires the
whole utterance to be a command, so anything embedded in a sentence cannot fire.

This is consistent with Karl's own hardware test on 2026-08-06: a video of people talking at ~25%
below his voice produced no inadvertent slide changes. Continuous speech is safe; isolated
fragments are not.

## Residual risk, stated plainly

Someone answers a question with a short fragment that rhymes with a command, and one slide moves.
The presenter sees it happen and "previous slide" corrects it.

**Worst case is the un-pause family** (phrases 22-25: "continue presenting", "consume the
presentation", "presume the presentation", "resume presenting"), which still fires 2-3 of 3 voices.
That reaches the presenter during the discussion window that "pause presentation" exists to protect.
Karl was offered a double-confirm mitigation on 2026-08-06 and chose to leave un-pause as-is —
fewest changes near a deadline. Recorded, not re-litigated.

## Verdict

Voice is fit for the 2026-08-12 talk under the conditions Karl described: ~20 people, discussion
paused deliberately, keyboard always available. The measurement is the basis for that, not optimism.
