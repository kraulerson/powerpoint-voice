# Performance Audit — Phase 3.5

**Date:** 2026-08-10
**Persona:** Power-Constrained User
**Commit:** 63fa398
**Measured on:** the development Mac mini (M-series). **The talk runs on an M3 Max**, which is
faster — so every number here is a pessimistic bound for showtime, not an optimistic one.
**Deck:** the real presentation deck, 10 slides. Timings only; no content is reproduced.

The persona for this step assumes a constrained machine. This application's constraint is not
a slow CPU — it is that **a presenter is standing in front of a room**, so the only latency
that matters is the kind they have to apologise for.

---

## 1. Opening a deck

| | |
|---|---|
| Parse, 5 runs | **min 1.0 ms · mean 1.5 ms · max 3.6 ms** |
| Slides | 10 |

The parse is off the UI thread regardless (TM-018), so this is not a blocking cost. At 1.5 ms
it would not be one even if it were.

## 2. Rasterising slides

| Target | Total, 10 slides | Mean | Worst single slide |
|---|---|---|---|
| **3840×2160 (4K)** | **298 ms** | 29.8 ms | **194 ms** (slide 1) |
| **1920×1080 (a typical projector)** | **214 ms** | 21.4 ms | **173 ms** (slide 1) |

Slide 1 is the worst by a wide margin because it carries the full-height photograph. **194 ms
for the slide the presenter is looking at while the room settles** is not a number anyone will
notice, and every other slide is pre-rendered behind it.

Rendering starts at the slide being shown and works outward (`renderOrder`), so presenting
begins immediately rather than after slide 1..N have all been rasterised.

## 3. Memory

| Target | Whole deck resident | Per slide |
|---|---|---|
| 4K | **316.4 MB** | 31.64 MB |
| 1080p | **79.1 MB** | 7.91 MB |

The raster budget (BUG-22) is **2 GB**. This deck at 4K uses **15.5%** of it, so no eviction
occurs at any point during this talk — the whole deck stays resident and every slide change is
instant.

The budget exists for a different deck: at 31.64 MB/slide, a 300-slide 4K deck would be
~9.3 GB, the machine would swap, and the OOM killer would take the application mid-talk.

## 4. Headroom against the TM-018 render caps

Worst slide in the deck, against each cap:

| Cap | Worst slide | Cap | Used |
|---|---|---|---|
| Shapes | 21 | 2000 | **1.05%** |
| Text runs | 32 | 5000 | **0.64%** |
| Characters | 523 | 200000 | **0.26%** |
| Declared image pixels | 987,730 | 200,000,000 | **0.49%** |

**Between two and three orders of magnitude of headroom on every axis.** The caps cannot fire
on this deck; they exist for a hostile or pathological one.

## 5. Voice — the only hard real-time path

The audio callback delivers a buffer roughly every 20 ms. If `feed()` takes longer than the
buffer represents, the pipeline falls behind real time and never recovers: commands arrive
late, then later, then not at all.

Measured over **30 seconds of audio, 1500 buffers**, through the real Vosk engine and the real
vendored model:

| | |
|---|---|
| Model load + recogniser create | **138 ms**, once, when voice is armed |
| Mean per 20 ms buffer | **0.094 ms** |
| **Worst** per 20 ms buffer | **5.275 ms** |
| **Real-time factor** | **0.0047** |

**The decoder uses under half a percent of the time it has**, and its worst single buffer used
26% of the budget. There is no plausible circumstance on an M3 Max in which the voice path
falls behind real time.

The 138 ms model load happens at arm time, not per command, and is off the presentation path.

---

## Findings

| ID | Severity | Finding |
|---|---|---|
| **PERF-1** | SEV-4 | Qt logs *"Populating font family aliases took 103 ms. Replace uses of missing font family 'Sans Serif'"* on first render. A one-off 103 ms during pre-render, off the UI thread. Related to BUG-33 (*"slides render a bit slowly"*, reported over RustDesk) and to BUG-30/34's font-database race — resolving deck font families to installed families ONCE on the GUI thread would remove this cost and shrink that race's window. Post-talk (F7c) |

Nothing else. No finding blocks the talk.

---

## What was NOT measured

- **On the target hardware.** Everything here is the Mac mini. The M3 Max is faster, so these
  are upper bounds — but they are not measurements of the machine the talk runs on.
- **Sustained runtime.** The longest continuous run measured is 30 s of audio. Nobody has run
  this application for the length of an actual talk and watched memory.
- **A large deck.** 10 slides. The 2 GB budget and the eviction order have unit tests, but no
  300-slide deck has ever been opened.
- **Cold start.** Timings are warm; the first launch after a boot will be slower by whatever
  the OS takes to page in Qt and the 40 MB model.
