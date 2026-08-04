# Security Audit Findings — Feature: F2/F3 Voice-Command Matcher & Dispatch

**Feature:** F2-F3-voice-commands (`matchCommand` + `RecognizerController`)
**Date:** 2026-08-04
**Auditor Personas:** Senior Security Engineer — 2 parallel adversarial agents
(matcher false-trigger/DoS; controller state-machine/leakage) + Semgrep SAST

The pure command layer: text from the recognizer (later, the keyboard) → a
closed-grammar `Command`, dispatched through a listening-state gate. No untrusted
binary, no file/network/persistence in this layer. The risk is (a) a false trigger
that moves the deck during a live talk, and (b) a crash/stall at the recognizer
call boundary.

---

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | p/owasp-top-ten, p/security-audit | Pass | 0 findings (re-run after remediation) |
| ASan + UBSan | Debug build, command-layer suite | Pass | 0 sanitizer errors (135 assertions) |

## Manual Review Findings — Matcher (`command_matcher`)

| # | Category | Finding | Severity | Resolution | Status |
|---|----------|---------|----------|------------|--------|
| M-MED-1 | Availability | Terminal punctuation / capitalization from dictation silently no-op'd every command ("Next slide." → nullopt; "Go to slide 5." → nullopt) — the presenter says the right command and nothing happens | Medium | Strip leading/trailing punctuation + exposed whitespace in normalization (both fixed and go-to paths); fails-safe preserved (internal punctuation still a safe miss, no new false trigger) | Fixed |
| F-LOW-1 | False-trigger | `GoToSlide` uses a prefix match; an utterance starting literally "go to slide " followed only by fillers + a number would jump ("go to slide seven show me" → 7) | Low | Not audience-plausible (essentially the command itself); `parseSlideNumber` aborts on the first non-filler token. Accepted; noted for a future tail-tightening if desired | Deferred |
| M-LOW-2 | Availability | Fixed commands tolerate zero filler ("please next slide" → nullopt) while go-to ignores fillers — inconsistent | Low | Deliberate product decision: strict fixed phrases minimize false-trigger surface; the grammar-constrained recognizer emits clean phrases | Deferred (by design) |
| M-LOW-3 | Robustness | Zero-width / format chars (U+200B, U+FEFF) survive `simplified()` (category Cf), so a pasted phrase with them misses | Low | Keyboard/paste-only (STT cannot emit them) and fails-safe (a miss, never a false match). Noted | Deferred |
| — | False-trigger | Homoglyphs, NBSP/Unicode spaces, Turkish-I locale folding, in-sentence keywords | — | **Confirmed clean:** exact-match on the normalized string means junk can only break equality (a miss), never forge a match; `toLower()` is locale-independent (no dotless-I hazard); `simplified()` collapses NBSP/U+3000/etc. | No defect |
| — | Correctness | Non-jump commands carry `slideNumber == 0`; no negative/overflow producible (bounded by F4) | — | Confirmed correct | No defect |

## Manual Review Findings — Controller (`RecognizerController`)

| # | Category | Finding | Severity | Resolution | Status |
|---|----------|---------|----------|------------|--------|
| S3 | Availability / Crash | The sink runs synchronously inside `onPhrase`, which the real audio-thread recognizer calls. A sink exception would cross that boundary → UB / `std::terminate` — a crash mid-presentation | High | Wrap the sink call in `try/catch(...)` so no exception escapes `onPhrase`; state is committed before the sink, so a failed sink leaves a consistent controller. No heard text logged (Bible §8) | Fixed |
| S2 | Correctness / DoS | A sink that synchronously re-feeds a phrase would re-enter `onPhrase` (double dispatch / unbounded recursion) | Medium | `inDispatch_` reentrancy guard drops re-entrant deliveries; preserves "at most one Command per top-level onPhrase" | Fixed |
| S6 | Concurrency | `state_` is non-atomic; the documented design has the audio thread writing it (onPhrase) while the UI reads it (state()) → data race | Medium | Documented same-thread contract on the class + `IRecognizer` (adapter must marshal phrases onto the controller's thread, e.g. Qt queued connection); enforced when the engine adapter is built | Fixed (contract) |
| S7 | Correctness | Vosk emits partial + final results; forwarding partials would fire one utterance as multiple nav jumps | Medium | Documented `IRecognizer` contract: deliver only finalized phrases, one per utterance; optional debounce is an engine-feature defense | Fixed (contract) |
| S5 | Robustness | If a sink destroyed the controller during dispatch, the post-sink guard reset would touch freed memory | Low | Documented precondition (sink must not destroy the controller during dispatch); state committed before sink | Fixed (contract) |
| S9 | Robustness | `switch` over `CommandType` has no `default` | Low | Intentional: kept exhaustive with no default so `-Wswitch` flags a future 6th enumerator at compile time (stronger than a runtime default). Commented | No defect (by design) |
| S1 | Safety invariant | "Paused blocks nav" airtight; state total/deterministic; no double-dispatch | — | Confirmed clean (the core Q&A-protection property) | No defect |
| S8 | Data isolation | Any heard-text / deck-content logging? | — | **Confirmed clean:** no logging/IO in `src/command/`; `Command` carries no text, only `{CommandType, int}` | No defect |

## Threat Model Cross-Reference

| Threat ID | Relevant? | Notes |
|-----------|-----------|-------|
| TM-002 / TM-019 (audience / mis-heard voice → wrong action) | Yes | The matcher's phrase-level exact match (no substring) is the front-line defense and audited clean — audience speech containing "next" does not fire. The controller's Paused state gates nav during Q&A (S1, airtight). S7's finals-only contract prevents one utterance firing multiple jumps. Reinforced. |
| TM-018 (availability is the top asset for a live talk) | Yes | S3 (sink-exception backstop) and S6 (threading contract) directly protect mid-talk availability — the app must not crash or stall when a command fires. M-MED-1 restores the command actually working after dictation punctuation. |
| TM-011 / TM-013 (Confidential-data leakage) | Yes | S8 confirms no heard text or deck content is logged or persisted by this layer. |

## Summary

| Status | Count |
|--------|-------|
| Fixed (code) | 3 (M-MED-1, S3, S2) |
| Fixed (documented contract) | 3 (S6, S7, S5) |
| No defect (confirmed clean) | 5 (matcher false-trigger, correctness; S1, S8, S9) |
| Deferred (Low, by design/note) | 3 (F-LOW-1, M-LOW-2, M-LOW-3) |
| Open | 0 |

**All findings resolved:** Yes

(No open items. The 3 deferred items are Low severity, by-design or paste-only, and
logged above for future consideration.)

The two security-critical properties this feature exists to provide both held under
adversarial review: **audience speech cannot false-trigger a command** (exact-match
normalization; Paused gates nav), and **no heard text is logged** (Bible §8). The
remediated defects were an availability bug (dictation punctuation) and edge
hardening at the recognizer call boundary (exception safety, reentrancy, threading
and finals-only contracts) — each a concrete mid-talk failure the current tests now
guard against. The S6/S7 threading and finals-only contracts are carried forward as
binding requirements for the voice-engine adapter feature.
