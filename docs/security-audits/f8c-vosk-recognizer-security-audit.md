# Security Audit Findings — Feature: F8c Vosk Recognizer (grammar layer)

**Feature:** F8c-vosk-recognizer
**Date:** 2026-08-05
**Auditor Persona:** Senior Security Engineer

---

## Scope and threat framing

`src/command/vosk_recognizer.{hpp,cpp}` — grammar construction, model validation and
recogniser preconditions. The Vosk-linked decoding session is NOT in this slice.

**The adversary is the audience.** TM-002/TM-019: a room of executives is within
earshot of the presenter's microphone, and any word they say that the decoder can
emit becomes a candidate command. The control is that the decoder is *incapable* of
producing anything but the five commands — not that it prefers them.

Vosk fails **open** in three ways, none of which raises an error:
1. a model with a static graph (`graph/HCLG.fst`) silently ignores the grammar and
   decodes the full ~200 000-word vocabulary;
2. a grammar token outside the model's vocabulary is silently dropped;
3. malformed grammar JSON **segfaults** rather than returning a failure.

## Automated Scan Results

| Tool | Config | Result |
|------|--------|--------|
| Semgrep | auto (`--error`) | Pass, 0 findings |
| ASan + UBSan | full suite | Pass, 236/236 |

## Manual Review Findings

Scope note: F8c-6 concerns the LIVE DECODE, which this slice does not contain — nothing links
`libvosk` yet. It is listed for completeness, scoped OUT of this feature, and tracked as BUG-65
against the decode slice. It is not an unresolved finding of the code being audited here.

| # | Category | Finding | Severity | File:Line | Resolution | Status |
|---|----------|---------|----------|-----------|------------|--------|
| F8c-1 | Threat Model / Fail-open | A static-graph model makes the grammar a no-op — the decoder listens to everything, silently. This is the audience-controls-the-slides failure | **Critical** | `vosk_recognizer.cpp:63` | `modelIsGrammarCapable()` requires BOTH `graph/HCLr.fst` and `graph/Gr.fst`; `prepareRecognizer()` refuses with `ModelNotGrammarCapable` and voice stays OFF. A test asserts the VENDORED model passes, so a future model swap that loses the dynamic graph fails loudly | **Fixed** |
| F8c-2 | Memory Safety | Malformed grammar JSON segfaults Vosk. String interpolation would put a crash one typo away | **High** | `vosk_recognizer.cpp:40` | The JSON is BUILT character by character from an allow-list (`a-z` and space only); anything else is dropped rather than escaped, because the grammar is a closed list we author. Tests drive quote-injection, backslashes and tabs and assert the document stays well formed | **Fixed** |
| F8c-3 | Correctness / Drift | If the grammar and the matcher disagree, either a decodable phrase is unusable or a command is unreachable by voice | Medium | `vosk_recognizer.cpp:26` | A test runs every grammar phrase through `matchCommand()` and requires a command. A second test asserts NO word in the grammar lies outside the five commands' vocabulary | **Fixed** |
| F8c-4 | Fail-safe | A recogniser failure must never end a talk | **High** | `vosk_recognizer.cpp:11` | Every `RecognizerInitError` yields a message naming the keyboard as the way forward; voice simply stays off. Consistent with F8b's `captureErrorIsRecoverable` | **Fixed** |
| F8c-5 | Information Disclosure | An init failure naming the model path could land on the projector | Medium | `vosk_recognizer.cpp:11` | Closed vocabulary, five fixed strings. A test asserts no `/`, and a length bound | **Fixed** |
| F8c-6 | Threat Model | Vosk silently drops grammar tokens outside the model's vocabulary, quietly widening what is recognised | Medium | — | **Out of scope for this slice** (needs a loaded model; nothing links libvosk yet). Tracked as BUG-65 against the decode slice | **Deferred to F8c-decode** |

## Threat Model Cross-Reference

| Threat | Relevant? | Verified? | Note |
|---|---|---|---|
| TM-002 / TM-019 | **Yes — this is the feature's reason to exist** | **Yes** for the model and grammar preconditions; **No** for the live decode, which is the next slice | The two-word phrase rule and the Paused gate remain the other layers |
| TM-011 | Yes | Yes | Nothing is written; the model is read-only and extracted at build time |
| TM-012 / TM-013 | Yes | Yes | Closed error vocabulary; no heard text is stored or logged here |

## Not closed — carried

1. **BUG-65** — out-of-vocabulary grammar tokens are dropped silently by Vosk. Needs a loaded model.
2. **No live decode.** This slice ends at "the preconditions are safe". Nothing links `libvosk` yet.
3. **No hardware verification.** No microphone on this machine; UAT and Karl's MacBook Pro are the
   only instruments for the end-to-end property.

## Summary

| Status | Count |
|--------|-------|
| Fixed | 5 |
| Open | 0 |
| Out of scope, tracked as BUG-65 | 1 |

**All findings resolved:** Yes

(The scoped-out item, F8c-6, is not a finding against this code — it is a property of the live
decode, which this slice does not contain. It is tracked as BUG-65 and must be closed by the decode
slice's own audit.)
