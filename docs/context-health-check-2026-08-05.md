# Context Health Check — 2026-08-05 (after 6 features)

Required by CLAUDE.md every 3-4 features. Blocked the start of F8b, correctly.

## Features built (6)
F1a deck loader · F1b slide renderer · F4 number parser · F2/F3 command grammar + dispatch ·
F7a presentation funnel · F7b usable presenter · F8a audio capture format
(FEATURES.md counts 6 headed entries; F1a/F1b are recorded as one F1.)

## Features remaining to the MVP cutline
F8b audio capture · F8c Vosk recogniser + wiring · F6 keyboard parity · F5 transcript overlay,
listening glyph and pre-show check · F7c render hardening (BUG-21/22/23/29/33/34/42/44/54/55).

## Data model — current, verified against the code
`Presentation{slideWidth, slideHeight, slides[], warnings[]}`;
`Slide{background, elements[], warnings[], placeholder}`;
`ShapeElement{kind, textBox|image|unsupported}`;
`ImageElement{rect, mediaPart, imageData, srcRect, alphaPerMille, stretchToFill}` — the last three
added this session by BUG-37/38/53; `Background{kind, solid, pictureMediaPart}`;
`AudioFormat{sampleRate, channels}` with bounds 8k-192k / 1-64.

## Verified AGAINST the Bible

| Bible claim | Reality | Verdict |
|---|---|---|
| five two-word commands + "go to slide N" | `command_matcher.cpp` matches exactly those, plus the "the"/"resume" variants added by BUG-11/17 | **agrees** |
| Vosk 0.3.44, grammar-constrained | vendored 0.3.44; `vosk_recognizer_new_grm` exported on both arm64 and x86_64; model carries `Gr.fst`+`HCLr.fst`, so grammar really constrains | **agrees** |
| fully offline, no network attacker | no network code anywhere; the new drop handler explicitly refuses remote URLs so a drag-and-drop could not become the first network path | **agrees** |
| **Qt 6.8 LTS** | every machine in this project builds on **Qt 6.11.1**; CMake's floor is 6.2 | **CONTRADICTED — Bible corrected** |

## Known issues
No open SEV-1. Open SEV-2: BUG-34 (font-database race residual), BUG-40 (flaky worker-thread test
group — has blocked two commits), BUG-42 (up to 5 s to quit, requests discarded in that window),
BUG-53 fixed, BUG-55 (BUG-41 fixed one of three paths). Full list in BUGS.md.

## Honest note on session length
This session is very long and has carried the whole of sessions 3-4. Nothing in the summary above
contradicts the codebase, and the single Bible contradiction was doc staleness rather than confused
state. Continuing is defensible — but the resume path (WALK-STATE.md) is kept current for exactly
this reason and should be trusted over recollection.
