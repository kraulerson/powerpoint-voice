All experiments complete. Report follows.

---

# UAT-5 — LENS: CAN THE DECODER EMIT ANYTHING OUTSIDE THE FIVE COMMANDS?

**Answer: YES, routinely — and worse, ordinary English fires real commands at confidence 1.000.** The shipped safety claim is false. I found the machine has `/usr/bin/say`, so "no microphone" was not a limit: I drove the real decoder with **real synthesised speech** (3 voices, 16 kHz mono LEI16, straight into `VoskEngine::feed`) as well as noise.

Harness (mine, no project file touched): `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat5/attack.cpp` + `CMakeLists.txt`, corpora `gen.sh`/`gen2.sh`/`gen3.sh`, logs `synth.log`, `tts_clean.log`, `near.log`, `presenter.log`, `talk.log`, `talk2.log`. It compiles the project's own `vosk_recognizer.cpp` / `vosk_engine.cpp` / `command_matcher.cpp` / `number_parser.cpp` and calls `prepareRecognizer()` → `VoskEngine::start()` → `feed()` in 3200-sample chunks, exactly as `VoicePipeline` does.

---

## BUG-66 — SEV-1 — VERIFIED — the decoder is NOT incapable of emitting non-commands

`CHANGELOG.md:23-26`: *"created with `vosk_recognizer_new_grm` against a dynamic-graph model, **so it is incapable of emitting anything but the five commands**."* Same claim in `src/command/vosk_recognizer.hpp:13-15`. **False.**

12 minutes of synthetic vowel babble / noise through the real engine produced **51 final results, 28 distinct strings, none a command**: `"eight resume"`, `"zero eighty"`, `"resume the eight"`, `"eleven twelve"`, `"eleven zero eighty slide"`, `"twelve twelve three"`, `"the"`, `"go"`, `"slide zero"`. `"eight resume"` is not adjacent in any grammar phrase — it requires backoff.

Root cause, and the project already knew it — `docs/design-notes/voice-engine-design.md:46`:
> *"Vosk grammar mode is a bigram LM (ngram_order=2, discount=0.5), **NOT an exact phrase matcher** — it can emit cross-phrase sequences like 'next presentation'. It is a strong **SOFT** vocabulary constraint."*

Confirmed in the binary: `strings build/vosk/libvosk.dylib` → `LanguageModelEstimator`, `opts_.ngram_order >= 2`. The grammar is a word list compiled into a backoff bigram, not a phrase FSA.

What **does** hold: the *lexical* constraint. Across 404 utterances from every corpus, 39 distinct words were emitted and **zero fell outside the grammar vocabulary** (`vocab.py`). So the correct claim is "closed vocabulary, open word order" — not "incapable".

Repro: `./b/attack long 12`

---

## BUG-67 — SEV-1 — VERIFIED — no `[unk]`: ordinary English is force-fit onto commands

`grammarPhrases()` (`src/command/vosk_recognizer.cpp:31-59`) has **no `[unk]` token**. The decoder therefore has no legal path for out-of-grammar audio and must snap every utterance onto the nearest command.

**98 of 102 near-miss utterances (34 phrases × 3 voices) fired a real `matchCommand()` command:**

| spoken | decoded | command |
|---|---|---|
| next flight / next light / next line / next site / next slot / next slice / next sled / next slap | `next slide` | NextSlide |
| annex slide / nexus slide / neck slide / vexed side / text side | `next slide` | NextSlide |
| obvious slide / devious slide / previous sign / previous size / previous slot | `previous slide` | PreviousSlide |
| applause presentation / pause president / paws presentation / cause presentation | `pause presentation` | PausePresentation |
| continue presenting / consume the presentation | `continue (the) presentation` | ContinuePresentation |
| presume the presentation / resume presenting | `resume (the) presentation` | ContinuePresentation |
| goat to sleep five / go to sled five / go to slime nine | `go to slide five` / `nine` | GoToSlide |

`continue presenting` and `presume the presentation` un-pause the deck — that is **TM-002/TM-019 firing through its own front-line defense**, during the Q&A window the Paused state exists to protect.

The design note predicted this **verbatim** (`voice-engine-design.md:46`):
> *"`[unk]` is included in the phrase list as a **mandatory** third element: without it the decoder has no legal path for out-of-grammar audio and **must force-fit every cough and audience question onto the nearest command — typically 'next slide' — which is threat TM-002/019 firing through its own front-line defense**."*

`voice-engine-design.md:58` further requires `validateGrammarJson()` to *reject a missing `[unk]`*. Neither shipped.

**The obvious fix is booby-trapped.** `grammarJson()` (`vosk_recognizer.cpp:76-86`) allows only `[a-z ]`, so:
```
grammarJson(["[unk]"])              = ["unk"]
grammarJson(["next slide","[unk]"]) = ["next slide", "unk"]
```
The brackets are silently stripped; Vosk then drops the OOV token `unk` with a `KALDI_WARN` — the BUG-65 guard does **not** catch it, because `unknownGrammarWords()` inspects the *phrases*, not the sanitised JSON. Adding `[unk]` naively yields no `[unk]` and no error. (Repro: `./b/attack diag`)

**Honest scope:** the same near-misses embedded mid-sentence without a pause fired **0/51** (`wav5/e_*`), because `matchCommand` requires the whole utterance. The hazard is any phrase that is *endpointed alone* — i.e. ≥~0.5 s of silence either side. That is extremely common (short interjections, sentence-final fragments, an audience member answering "next flight"). Isolated `"Next slide."`, `"Previous slide."`, `"Pause presentation."` from an audience voice all fired (`near.log`, `c_aud_049`-`052`).

**Partial mitigation measured, not sufficient:** re-running the same 102 with `[unk]` appended (`PPTV_GRAMMAR_OVERRIDE`) dropped hits from 98 → **85**. It rescues `continue presenting`, `previous sign`, `pause president`, `go to slide fine`; it does **not** rescue `next flight`, `next light`, `annex slide`, `obvious slide`, `goat to sleep five`. A grammar-only defense cannot deliver the claimed property. The CHANGELOG claim must be withdrawn regardless of what is fixed.

Repro: `./gen3.sh && ./b/attack wav wav3/c_*.wav`

---

## BUG-68 — SEV-1 — VERIFIED — every force-fit reports `conf: 1.000000`

Raw-API run with `vosk_recognizer_set_words(r,1)` on the identical grammar:

```
a_001_next-slide      "next":conf 1.000000  "slide":conf 1.000000   -> "next slide"
c_near_004_next-flight "next":conf 1.000000  "slide":conf 1.000000   -> "next slide"
c_near_013_annex-slide "next":conf 1.000000  "slide":conf 1.000000   -> "next slide"
c_near_128_applause…   "pause":conf 1.000000 "presentation":1.000000 -> "pause presentation"
c_near_137_goat-to-sleep-five  go/to/slide/five all conf 1.000000    -> "go to slide five"
```

A genuine "next slide" and someone saying "next flight" are **bit-identical** in confidence. `textFromVoskResult` (`vosk_engine.cpp:14-23`) deliberately discards `conf` — but the point is stronger than "discarded": with a closed grammar and no `[unk]` there is no competing hypothesis, so the posterior is 1.0 by construction. **A confidence threshold cannot be retrofitted until `[unk]` exists.** This forecloses the cheapest-looking fix.

Repro: `./b/attack conf wav/a_001_next-slide.wav wav3/c_near_004_next-flight.wav`

---

## BUG-69 — SEV-2 — VERIFIED — spurious `GoToSlide` from ordinary conference prose

7.3 minutes of 30 invented conference sentences × 3 voices, concatenated with 0.35–1.10 s pauses and room tone, through one session, emitted:

```
"go to slide zero two three go six"
```
`build/command_probe "go to slide zero two three go six"` → **`GoToSlide(236)`**

Chain: the bigram sprays `go`/`to`/`slide`/number words → `matchCommand` sees the `"go to slide "` prefix → `parseSlideNumber("zero two three go six")` treats `go` as filler (`number_parser.cpp:32-39` lists `go`, `to`, `slide`, `the` — precisely the words the LM sprays most), leaving `0,2,3,6` → all-single-digits → `"0236"` → 236.

Rate: 1 event in ~140 min of continuous prose across 17 runs (seed-sensitive; ~0.3 expected events per 45-min talk — wide CI on n=1). Bounded but not harmless: `presentation_controller.cpp:57-61` rejects out-of-range and shows a transient notice **on the projector mid-talk**; an in-range salad would actually jump.

Repro: `./b/attack talk 0.35 1.10 12345 wav2/b_*.wav` (see `talk.log`)

---

## BUG-70 — SEV-2 — VERIFIED — the presenter's own documented phrasings are dead, and the tests hide it

**40 of 60** presenter utterances failed. Every phrasing with a discourse/politeness word failed **3/3 across all voices**:

| said | decoded | result |
|---|---|---|
| next slide please | `next slide previous` | no command |
| okay next slide | `go next slide` / `eighteen next slide` / `go to next slide` | no command |
| okay next slide please | `go next slide previous` | no command |
| alright next slide | `hundred next slide` / `four five next slide` | no command |
| so next slide | `zero next slide` | no command |
| next slide everyone | `next slide three one` | no command |
| previous slide please | `previous slide previous` | no command |
| okay previous slide | `go previous slide` | no command |
| pause the presentation please | `pause the presentation previous` | no command |
| okay pause presentation | `go pause presentation` | no command |
| please continue the presentation | `pause continue the presentation` | no command |
| go to slide five please | `go to slide five previous` | no command |

The filler words are not in the grammar, so the decoder **must** substitute an in-vocabulary word — systematically `please`→`previous`, `okay`→`go`/`go to`/`zero`. The substitute is not in `leadingFiller()`/`trailingFiller()` (`command_matcher.cpp:26-43`), so the exact-phrase test fails.

**The entire UAT-2 BUG-11/12 filler-strip fix is unreachable on real audio** — `matchCommand` can never see `okay` or `please`, because the decoder is physically incapable of emitting them.

And `tests/test_command_matcher.cpp:154-163` asserts exactly these strings and **passes**:
```cpp
checkCmd(matchCommand(QStringLiteral("okay next slide")), CommandType::NextSlide);
checkCmd(matchCommand(QStringLiteral("next slide please")), CommandType::NextSlide);
checkCmd(matchCommand(QStringLiteral("go to slide five please")), CommandType::GoToSlide, 5);
```
This is the blind spot UAT-4's 33 mutations could not reach: they mutate the matcher against strings the decoder cannot produce. **Any test that hands `matchCommand` a string containing an out-of-grammar word is testing a path that cannot occur in production.**

Repro: `./b/attack wav wav4/d_*.wav` (see `presenter.log`)

---

## Negative results (the safe parts — VERIFIED)

- **Pure silence is clean.** 20 minutes of digital silence → **0** final results. 10 s → 0. No spurious result from long silence.
- **Non-speech alone never fired a command.** White/pink noise at amp 200→60000 (incl. clipped), tones 120/440/1000 Hz, 90/200 Hz clipped squares, chirps up and down, ±full-scale DC, impulse trains, alternating silence/noise, speech-shaped noise at 2/4/7 Hz syllabic rates → **0 results** except formant babble (11 sub-command strings) and one 90 Hz square (`"go"`). Across 12 min of mixed babble: 51 results, **0 commands**.
- **Vocabulary containment holds.** 0 of 39 emitted word types outside the grammar.
- **BUG-65 guard and the static-graph refusal work as documented.**
- **Out-of-range jumps are rejected, not clamped** (`presentation_controller.cpp:57-61`).

---

## Bottom line

The feature's stated reason to exist — *"the decoder must be INCAPABLE of emitting anything but the five commands"* — **does not hold, and the project's own design note says so at `voice-engine-design.md:46`.** The one mitigation that note marks **mandatory** (`[unk]`) is absent from the shipped grammar, and `grammarJson()`'s allow-list silently destroys it if added. The measured consequence is that a room full of executives saying `next flight`, `next line`, `obvious slide`, `applause`, `the president`, or `continue presenting` moves the presenter's deck, at maximum reported confidence, while the presenter's own `"next slide please"` does nothing.

Minimum before this ships: withdraw the "incapable" claim in `CHANGELOG.md:23-26` and `vosk_recognizer.hpp:13-15`; add `[unk]` **and** widen `grammarJson()` to preserve it **and** assert its presence in the emitted JSON; add the grammar's filler words so the documented phrasings work; and add an end-to-end test that drives `VoskEngine` with synthesised audio rather than feeding `matchCommand` strings the decoder cannot produce (the harness above is a working template).

**TL;DR (plain English):** The app's headline safety promise is that the speech engine physically cannot hear anything except the five slide commands. I tested it with the Mac's own text-to-speech and it isn't true. Someone in the audience saying "next flight", "next line", "obvious slide", "a round of applause", or "the president" will flip the presenter's slides — and the engine reports 100% certainty that it heard a real command. Meanwhile the presenter saying "next slide please" does nothing at all, because "please" isn't a word the engine is allowed to know, so it hears "next slide previous" and ignores it. The team's own design document warned about this exact problem and named the one-line fix (a special "I didn't catch that" token); the shipped code left it out, and the code that builds the word list would quietly delete it even if someone added it. Silence and background noise are safe — nothing spurious there. The bug list and changelog currently claim the opposite of what the software does, and the existing tests can't catch it because they test the text-matching half without ever running the actual listener.