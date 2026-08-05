# Security Audit Findings — Feature: F8a Audio Capture Format

**Feature:** F8a-audio-capture-format
**Date:** 2026-08-05
**Auditor Persona:** Senior Security Engineer

---

## Scope and threat framing

`src/audio/audio_format.{hpp,cpp}` — pure conversion from a capture device's native
format to the recogniser's required 16 kHz mono.

**The untrusted input here is not a file — it is the hardware.** A capture device
declares its own sample rate and channel count, and nothing in the operating system
guarantees those numbers are sane. A broken driver, a counterfeit USB interface, or a
deliberately hostile one can report anything. The audio *samples* are equally
attacker-influenced: anyone within earshot of the presenter's microphone can drive
them, which is the same adversary the command grammar already assumes (TM-002/TM-019).

This is also the first module whose failure mode is *silence rather than an error*:
a wrong conversion produces confident nonsense, not a crash.

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | auto (`--error`) | Pass | 0 findings on `src/audio/` |
| ASan + UBSan | full suite, `halt_on_error=1` | Pass | 207/207 tests, no reports |
| ASan + UBSan | targeted fuzz, 1132 hostile format combinations | Pass | no crash, no UB |

The fuzz pass (`/tmp/fuzz_af.cpp`, not committed) sweeps the cross product of
`{0, -1, 1, 8000, 16000, 44100, 48000, 96000, 192000, INT_MAX, -48000}` rates ×
`{0, -1, 1, 2, 3, 6, 8, 64, 1024, INT_MAX}` channels × several frame counts, plus
extreme up/down ratios, through all three entry points.

## Manual Review Findings

| # | Category | Finding | Severity | File:Line | Resolution | Status |
|---|----------|---------|----------|-----------|------------|--------|
| F8a-1 | Input Validation / Resource Exhaustion | **Unbounded allocation amplification from a device-reported sample rate.** `resampleMono` sizes its output as `in.size() * outRate / inRate`. A device reporting `inRate = 1` turns a single 4096-frame callback into 65 536 000 samples — 131 MB — and the callback repeats continuously, so the machine dies. The rate is attacker-controlled in the sense that matters: nothing verifies what the device claims | **High** | `src/audio/audio_format.cpp:31` | Bounded BOTH rates to 8 000–192 000 Hz and channels to 1–64 (`kMinSampleRate`/`kMaxSampleRate`/`kMaxChannels`, `audio_format.hpp:31-33`). These are a resource cap in the same family as the loader's decompression caps. No real capture device reports outside them. `AudioFormat::isValid()` enforces the same bounds so the whole conversion path rejects rather than converts | **Fixed** |
| F8a-2 | Input Validation | `downmixToMono` divides by `channels`; a device reporting 0 would divide by zero | Medium | `src/audio/audio_format.cpp:11` | Guarded before use, and `channels > kMaxChannels` also rejected so the divisor is always in `[1, 64]`. Covered by `AF/audit F8a-1` subcase "an absurd channel count is rejected before it is used as a divisor" | **Fixed** |
| F8a-3 | Integer Overflow | Summing interleaved channels for the downmix overflows `int16` — two full-scale channels sum to 65 534. The wrap is not a crash; it is an audible click on exactly the loudest frames, which would degrade recognition precisely when the presenter speaks up | Medium | `src/audio/audio_format.cpp:18` | Accumulated in `std::int32_t`, then divided. Pinned by the test "downmix does not overflow on loud input" using `INT16_MAX`/`INT16_MIN` input | **Fixed** |
| F8a-4 | Memory Safety | The interpolating resampler reads `in[idx + 1]`; at the final output position `idx + 1` can be one past the end | Medium | `src/audio/audio_format.cpp:47` | Bounds-checked; the final sample is held rather than read past the end. ASan-clean across the fuzz sweep | **Fixed** |
| F8a-5 | Data Isolation / Privacy | Audio buffers are the most sensitive data this application will ever hold — everything said in the room, including during Q&A. A buffer written to disk, or included in a log line or error message, would be a disclosure with no upper bound on content | **High** | whole module | The module performs **no I/O of any kind**: no file handles, no logging calls, no exception messages carrying sample data. Verified by inspection and by `grep` for `qDebug`/`printf`/`ofstream`/`QFile` across `src/audio/` — zero hits. Buffers are `std::vector` locals that die with the call. This preserves TM-011 (no disk cache) and Bible §8 by construction rather than by discipline | **Fixed (by design)** |
| F8a-6 | Logging | A conversion failure returns an empty vector and says nothing. Deliberate: an error path that described the device or the audio would be the disclosure channel F8a-5 exists to prevent. The *caller* is responsible for a closed-vocabulary `NoticeId` if the presenter needs telling | Low | whole module | Accepted by design; consistent with `src/present/notice.hpp`'s closed vocabulary | **Accepted** |

## Threat Model Cross-Reference

| Threat ID | Relevant to This Feature? | Mitigation Verified? | Notes |
|-----------|--------------------------|---------------------|-------|
| TM-002 | Yes | N/A at this layer | Audience false-trigger. This module only reshapes samples; the grammar constraint and the two-word phrase rule are the controls, and they live upstream in `command_matcher` / the Vosk grammar |
| TM-011 | Yes | **Yes** | No disk cache: the module opens no files and writes nothing. Audio exists only as call-scoped vectors |
| TM-012 | Yes | **Yes** | No content disclosure: no logging, no error strings, nothing derived from samples leaves the function |
| TM-013 | Yes | **Yes** | Nothing from this module can reach an audience-facing surface — it returns samples to its caller and nothing else |
| TM-014 | Yes (by analogy) | **Yes** | Resource exhaustion. The rate/channel caps are the audio equivalent of the loader's decompression caps — see F8a-1 |
| TM-018 | Yes | **Yes** | The conversion is O(n) in the buffer with a bounded constant, so it cannot stall its thread. It will run on the capture thread, never the GUI thread |
| TM-019 | Yes | N/A at this layer | Same as TM-002 |

## Notes for the next feature (F8b, the capture device itself)

Findings that are **not** in scope here but that F8b must answer, recorded so they are
not lost between features:

1. **`NSMicrophoneUsageDescription` is mandatory** — macOS terminates the process
   without it. Already fixed pre-emptively (BUG-45); F8b must not regress it.
2. **Bind to the default device through the API**, never a device index, name, or
   enumeration order. The talk machine is not the development machine.
3. **A device change mid-talk** (AirPods connecting, a headset unplugged) changes the
   default device. F8b must not crash and must not silently stop listening.
4. **The capture callback is a real-time thread.** No allocation, no locks, no Qt
   objects. The conversion here allocates, so it belongs off that thread — the
   callback should hand raw frames to a queue.
5. **Microphone permission can be denied.** That is a normal state, not an error to
   crash on: the keyboard remains the guaranteed control path.

## Summary

| Status | Count |
|--------|-------|
| Fixed | 4 |
| Fixed (by design) | 1 |
| Accepted (with rationale) | 1 |
| Open | 0 |

**All findings resolved:** Yes
