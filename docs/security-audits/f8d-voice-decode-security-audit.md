# Security Audit Findings — Feature: F8d Voice Decode

**Feature:** F8d-voice-decode
**Date:** 2026-08-05
**Auditor Persona:** Senior Security Engineer

---

## Scope and threat framing

`src/command/vosk_engine.{hpp,cpp}` (the live decoder), `src/audio/voice_pipeline.{hpp,cpp}`
(the join), and the `AppShell::armVoice()` wiring.

This is the feature that makes the application listen. Two adversaries:

1. **The audience.** They are within earshot of the microphone and every word they
   say that the decoder can emit is a candidate command (TM-002/TM-019).
2. **Any path out of the process.** The decoder now produces TEXT of what was said
   in the room, including Q&A. A single log line or rendered string is a
   confidentiality breach with no upper bound on content (Bible §8, TM-012/013).

## Automated Scan Results

| Tool | Config | Result |
|------|--------|--------|
| Semgrep | auto (`--error`) | Pass, 0 findings |
| ASan + UBSan | full suite | Pass |

## Manual Review Findings

| # | Category | Finding | Severity | File:Line | Resolution | Status |
|---|----------|---------|----------|-----------|------------|--------|
| F8d-1 | Threat Model / Fail-open | **BUG-65** — Vosk drops grammar words the model does not know, SILENTLY widening what can be recognised | **High** | `vosk_engine.cpp:70` | Every grammar word is now round-tripped through `vosk_model_find_word()` before the recogniser is created; any unknown word aborts with `GrammarRejected` and voice stays off. Verified against the real vendored model: it passes, so the grammar is fully in-vocabulary | **Fixed** |
| F8d-2 | Information Disclosure | Vosk logs decoder internals — including heard words — to stderr by default | **High** | `vosk_engine.cpp:56` | `vosk_set_log_level(-1)` in the constructor, before any model is loaded | **Fixed** |
| F8d-3 | Information Disclosure | Recognised text must not be stored, logged or rendered | **High** | `voice_pipeline.cpp:60`, `app_shell.cpp` | The phrase goes from the decoder straight into `RecognizerController` and is never assigned to a member, written, or passed to a widget. `phraseHeard` is documented "NEVER log this". Only `Notice` ids reach any surface, and those carry integers | **Fixed (by design)** |
| F8d-4 | Concurrency | Decoding happens on the audio consumer thread; everything downstream is GUI-thread only | **High** | `voice_pipeline.cpp:64` | The phrase crosses via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Tests assert decoding happens OFF the GUI thread and the signal arrives ON it | **Fixed** |
| F8d-5 | Memory Safety | `vosk_recognizer_accept_waveform_s` takes an `int` length; a larger buffer would truncate or wrap | Medium | `vosk_engine.cpp:118` | Refused above `INT_MAX` rather than truncated | **Fixed** |
| F8d-6 | Memory Safety | A capture callback arriving after teardown would use a freed engine | **High** | `voice_pipeline.cpp:38` | `stop()` stops the DEVICE before anything is released, and `running_` is cleared first so a callback already in flight returns immediately. A test drives samples after `stop()` and asserts zero decodes | **Fixed** |
| F8d-7 | Availability | A voice failure must never delay or prevent the presentation | **High** | `app_shell.cpp` | Voice is armed on a QUEUED call after the window is shown and rendering has begun; any failure sets an operator-facing reason and is otherwise ignored. The keyboard path is untouched | **Fixed** |
| F8d-8 | Least Privilege / UX | The microphone permission prompt could land before there is any UI | Medium | `app_shell.cpp` | Voice is armed only after the presentation window exists. The model and grammar are validated BEFORE the microphone is opened, so a model that cannot constrain never triggers a prompt for a capability we are about to refuse | **Fixed** |

## Threat Model Cross-Reference

| Threat | Relevant? | Verified? | Note |
|---|---|---|---|
| TM-002 / TM-019 | **Yes — the core threat** | **Partially.** The grammar is enforced (model is dynamic-graph, every word in-vocabulary, decoder cannot emit anything else) and the two-word phrase rule and Paused gate still apply. **NOT verified against a live microphone with real audience speech — no microphone on this machine.** | Carried to UAT |
| TM-011 | Yes | **Yes** | Nothing written to disk; the model is read-only, extracted at build time |
| TM-012 / TM-013 | Yes | **Yes** | Vosk logging silenced; heard text never stored, logged or rendered |
| TM-018 | Yes | **Yes** | Decoding is off the GUI thread; the deck cannot be stalled by audio |

## Not closed — carried to UAT-5 and the human arm

1. **No live-audio verification of any kind.** Every test uses silence or a fake decoder. That the
   grammar holds against a real room is unproven and unprovable here.
2. **No barge-in / device-change handling.** AirPods connecting mid-talk leaves the old device.
3. **Recognition latency is unmeasured.**

## Summary

| Status | Count |
|--------|-------|
| Fixed | 8 |
| Open | 0 |

**All findings resolved:** Yes

(The carried items are verification that requires hardware this machine does not have, not
unresolved defects in the code audited here. They are BUG-64's human arm.)
