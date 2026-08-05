# UAT Test Session — 1

**Date:** 2026-08-03
**Features Under Test:** F1a (Deck Loader), F1b (Slide Renderer)
**Tester:** Karl Raulerson (human) + 3 parallel agent-testers

> **Template note:** Markdown fallback used (not the HTML template). At this stage the app has
> no clickable UI — F7 wires load→render→screen together — so this session tests the loader +
> renderer library surface via the automated suite, sanitizers, agent exploration, and a
> **real-deck render preview**. The HTML template + lint apply to end-user UI scenarios and are
> deferred to the first post-F7 UAT session.

---

## Before you start (test environment)

- **System under test:** macOS (Darwin, Apple Silicon) — the showtime machine. Ubuntu also
  validated via CI. Library `pptv_core` + `render_preview` headless tool.
- **Project root:** `/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice`
- **Runtime / tooling:** C++20, Qt 6.11.1 (brew), CMake 4.4.2, libzip 1.11.4, pugixml 1.16.
- **Required tools:** cmake, ninja, Qt6, libzip, pugixml, pkg-config (all installed).
- **One-time setup:** `bash scripts/render-deck.sh <your-deck.pptx>` builds the preview tool and
  renders every slide to `render-out/slide-NNN.png`. The deck stays local — never committed.

---

## Test Scenarios

### Feature: F1a + F1b — Automated (agent-run)

| # | Scenario | Steps | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|---|
| 1 | Full unit/integration suite | `scripts/run-tests.sh` | 40/40 pass | Pass | loader, renderer, security regressions, 14 Karl-authored assertions |
| 2 | Memory safety under untrusted input | ASan+UBSan build, run suite incl. hostile fixtures | No sanitizer errors | Pass | clean under ASan+UBSan, 40 tests |
| 3 | Semgrep SAST | `semgrep p/owasp-top-ten p/security-audit src/` | 0 findings | Pass | |

### Feature: F1a + F1b — Real-deck fidelity (HUMAN — Karl)

| # | Scenario | Steps | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|---|
| 4 | Render the real executive deck | `scripts/render-deck.sh <real-deck.pptx>`; open each `render-out/slide-NNN.png` | Every slide visually matches PowerPoint: text present + correct color/position, images shown | _pending Karl_ | The from-scratch-renderer fidelity check on real content |
| 5 | Slide count + order | Compare PNG count and order to PowerPoint | Same count; same order | _pending Karl_ | "go to slide N" correctness depends on this |
| 6 | Unsupported-element report | Read the tool's `warning:` lines | Warnings name each table/chart/group | _pending Karl_ | |

### Feature: F1a + F1b — Exploratory (agent-run)

| # | Scenario | Steps | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|---|
| 7 | Real-PowerPoint OOXML compatibility | Assess loader vs real-deck structures | Theme colors, inheritance, groups, wrap handled | **Fail** | → BUG-1..7 |
| 8 | Multi-paragraph / multi-run / positioning / z-order / letterbox | Render varied synthetic decks | Faithful | Partial | stacking/position/z-order/letterbox CORRECT; multi-run color + wrap FAIL |

---

## Bugs Found

See `BUGS.md` (BUG-1..7). Summary:

| # | Severity | Feature | Description | Steps to Reproduce | Expected vs Actual |
|---|---|---|---|---|---|
| 1 | SEV-1 | F1a/F1b | Theme/inherited text color → invisible text on dark decks | Deck with `<a:schemeClr>` or no run color on a dark slide | Expected: visible text. Actual: black default → invisible |
| 2 | SEV-1 | F1a | Placeholder position inheritance not resolved → overlap/clip | Slide with layout-inherited (no inline xfrm) title/body | Expected: correct positions. Actual: 0,0,0,0 overlap, cx=0 clips |
| 3 | SEV-2 | F1a/F1b | Grouped shapes → placeholder box hides grouped text | Deck with `<p:grpSp>` containing text | Expected: text shown. Actual: grey placeholder |
| 4 | SEV-2 | F1b | No word-wrap → long text truncated | Text longer than its box | Expected: wrap. Actual: single line clipped |
| 5 | SEV-2 | F1b | Multi-run color/format lost | Paragraph with mixed-color runs | Expected: each run its color. Actual: first run's color only |
| 6 | SEV-2 | F1a | Line breaks `<a:br>` dropped | Text with soft line breaks | Expected: line break. Actual: concatenated |
| 7 | SEV-3 | F1a/F1b | Bullets/indent ignored | Bulleted list | Expected: bullets+indent. Actual: plain stacked lines |

---

## Overall Notes

The synthetic fixtures (minimal hand-built OOXML) passed everything, but they did not exercise
how **real** PowerPoint decks specify color (themes/inheritance), position (layout placeholders),
or structure (groups, wrapped bullets, line breaks). The renderer is memory-safe and correct on
what it supports; the gap is **coverage of real-deck OOXML features**. BUG-1 (invisible text) is
the ship-critical one for a live talk on a dark deck. Triage + remediation follow.
