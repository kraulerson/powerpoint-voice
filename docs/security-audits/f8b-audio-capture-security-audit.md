# Security Audit Findings — Feature: F8b Audio Capture

**Feature:** F8b-audio-capture
**Date:** 2026-08-05
**Auditor Persona:** Senior Security Engineer

---

## Scope and threat framing

`src/audio/audio_capture.{hpp,cpp}` (policy) and `src/audio/miniaudio_capture.{hpp,cpp}`
(the CoreAudio backend).

This feature opens **the microphone in the room where a confidential presentation is
given**. The audio it handles is the most sensitive data this application will ever
touch: everything said by the presenter and by the audience, including Q&A. The
adversary model is therefore not only the hostile device — it is any path by which
those samples could reach a disk, a log, a projector, or a network.

Second, the callback runs on a **real-time audio thread**. Code that blocks or
allocates there does not merely run slowly; it produces glitches, and in the worst
case stalls the audio unit.

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | auto (`--error`) | Pass | 0 findings on `src/audio/` |
| ASan + UBSan | full suite | Pass | 224/224 |
| Build | `-Wall -Wextra` via project defaults | Pass | no new warnings |

## Manual Review Findings

| # | Category | Finding | Severity | File:Line | Resolution | Status |
|---|----------|---------|----------|-----------|------------|--------|
| F8b-1 | Data Isolation / Privacy | Captured audio must never reach disk, a log, or a user-facing surface | **High** | whole feature | The capture path performs **no I/O of any kind** — no file handles, no logging, no error strings derived from samples. Verified by `grep` for `qDebug`/`printf`/`ofstream`/`QFile`/`std::cout` across `src/audio/`: zero hits. Samples exist only as a pointer + length passed to the sink and are never copied into a member. Preserves TM-011/012/013 **by construction** | **Fixed (by design)** |
| F8b-2 | Input Validation | The device reports its own sample rate and channel count and nothing verifies them | **High** | `miniaudio_capture.cpp:48` | The format is validated through `AudioFormat::isValid()` (8–192 kHz, 1–64 ch) before capture starts; an out-of-range device is refused with `UnsupportedFormat` rather than used. The bounds are the resource cap established by audit F8a-1 | **Fixed** |
| F8b-3 | Memory Safety | A buffer whose real length disagrees with the declared format is a heap over-read | **High** | `miniaudio_capture.cpp:112` | The callback computes the count from the device's CURRENT channel count and passes it explicitly; `toRecognizerFormat` refuses when the format's implied length exceeds what was supplied (BUG-56). Verified under ASan: removing that check produces a real `heap-buffer-overflow` | **Fixed** |
| F8b-4 | Concurrency | The sink is set from the GUI thread and invoked from the audio thread; a torn `std::function` would be a call through a half-written object | **High** | `miniaudio_capture.cpp:87,105` | Guarded by `sinkMutex_`. Uncontended in steady state because the sink is set once before `start()`. `stop()` stops the device **before** `uninit`, then clears the sink, so no callback can be in flight against a dying device | **Fixed** |
| F8b-5 | Availability / Real-time safety | A lock on the audio thread can glitch or stall capture | Medium | `miniaudio_capture.cpp:105` | Accepted, with rationale: the lock is held only for the sink call, is uncontended after start, and the alternative (a lock-free ring buffer) adds a substantially harder-to-verify component for a benefit no one can measure without the target hardware. **Revisit if UAT-4 on the MacBook Pro shows dropouts** | **Accepted** |
| F8b-6 | Error Handling / Availability | A microphone failure must not be able to end a talk | **High** | `audio_capture.cpp:35` | `captureErrorIsRecoverable()` returns true for **every** error, as a stated property with a test rather than an assumption spread across callers. The keyboard remains the guaranteed control path | **Fixed (by design)** |
| F8b-7 | Information Disclosure | An error message naming the device or an OS error code could land on the projector | Medium | `audio_capture.cpp:7` | The vocabulary is CLOSED — five fixed strings, none derived from the device. A test asserts no `/`, no `errno`, no `0x`, and a length bound | **Fixed** |
| F8b-8 | Least Privilege | macOS microphone permission | Low | `cmake/MacOSXBundleInfo.plist.in` | `NSMicrophoneUsageDescription` states that recognition is on-device and that nothing is recorded or transmitted. Refusal is handled as a normal state (F8b-6), never a crash | **Fixed** |

## Threat Model Cross-Reference

| Threat ID | Relevant? | Mitigation Verified? | Notes |
|-----------|-----------|---------------------|-------|
| TM-002 / TM-019 | Yes | N/A at this layer | Audience false-trigger. The controls are the grammar constraint and the two-word phrase rule, upstream of capture |
| TM-011 | Yes | **Yes** | No disk cache: the feature opens no files. The model is extracted at BUILD time precisely so nothing is written during a talk |
| TM-012 / TM-013 | Yes | **Yes** | No logging, no sample-derived strings, closed error vocabulary |
| TM-014 | Yes | **Yes** | Device-reported format bounded before use |
| TM-018 | Yes | **Yes** | Capture runs on its own thread; the GUI thread is never blocked by it |

## Not yet closed — carried to F8c/UAT-4

1. **Nothing consumes the sink yet.** This feature ends at "samples, converted, delivered".
   The recogniser is F8c. Until then GROUP AC and GROUP AF test code with no production caller
   (BUG-51), and the tests overstate what is actually protected at run time.
2. **A device change mid-talk** (AirPods connecting) is not handled — miniaudio will keep the old
   device. Not a safety defect; a usability one. Logged for F8c.
3. **No hardware verification is possible here.** The development machine has no microphone. Every
   finding above is reasoned from code and verified with a fake device or a sanitizer; none is
   verified against a real one. **UAT-4 on the MacBook Pro is the only instrument.**

## Summary

| Status | Count |
|--------|-------|
| Fixed | 5 |
| Fixed (by design) | 2 |
| Accepted (with rationale) | 1 |
| Open | 0 |

**All findings resolved:** Yes
