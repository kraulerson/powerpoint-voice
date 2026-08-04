# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/) with extended categories
for handoff clarity. Categories are ordered by impact severity.

<!--
  Category definitions:
  - Security: Vulnerability fixes, dependency patches for CVEs, auth changes
  - Data Model: Schema migrations, data format changes, rollback notes
  - Added: New features, new endpoints, new commands
  - Changed: Modifications to existing behavior
  - Fixed: Bug fixes (reference BUGS.md entry if applicable)
  - Removed: Removed features, deprecated endpoints
  - Infrastructure: CI/CD changes, dependency updates, configuration changes, tooling
  - Documentation: Significant doc updates (new ADRs, updated threat model, revised user guide)
-->

## [Unreleased]

### Security
- **F7b hardened after an adversarial audit** (all findings reproduced in real builds; ThreadSanitizer
  clean). Five Critical, all fixed: a use-after-free that made the app **SEGV on the first arrow key
  after pre-render** (Qt destroys workers on `QThread::finished`; raw pointers dangled → now
  `QPointer`); a 0-slide deck producing an **unquittable fullscreen black projector**; the deck's
  **full file path rendered into a dialog** that lands on the projector (Bible §8 / TM-013); an
  attacker-triggered use-after-free in the loader (`st.name` read after `zip_close`) that **printed
  freed heap into that dialog**; and media-part read amplification with a **~640 GB allocation
  ceiling** from a ≤200 MB file. Plus the privacy blackout never actually blanking, an invisible
  quit prompt that never timed out, and unbounded shutdown. Findings:
  `docs/security-audits/f7b-usable-presenter-security-audit.md`.
- **F7a presentation funnel hardened** after adversarial audit: `undoJump()` was a second
  index-computing entry point that moved the deck behind the quit overlay and the privacy blackout;
  the blackout could be dismissed by a *rejected* command and by a voice "continue presentation"
  while paused (an audience-Q&A disclosure path, TM-002/012/019); the quit prompt self-destructed on
  a real wall clock and its timer was signed-overflow UB. All fixed test-first and verified under
  ASan+UBSan. Findings: `docs/security-audits/f7a-presentation-funnel-security-audit.md`.
- **Command grammar requires an object (BUG-17):** a bare "pause"/"continue"/"resume" is no longer
  a command. Accepting single words gave the audience a ONE-WORD un-pause during Q&A — the exact
  window the Paused state exists to protect (TM-002/019) — and the filler strip reduced ordinary
  speech ("okay lets continue", "and now continue", "lets pause") to that lone word. Now
  "pause/continue/resume (the) presentation" is required; 7 regression assertions lock it. Found by
  the voice-engine design review; the residual stuck-in-Paused risk is covered by keyboard parity
  (F6), resequenced ahead of the voice engine.
- Voice-command layer (F2/F3) hardened at the recognizer boundary: the dispatch sink is
  exception-guarded so a throwing command handler can never cross the audio-thread boundary
  and crash the app mid-talk; a reentrancy backstop prevents double-dispatch/recursion; the
  `IRecognizer` contract pins finalized-phrases-only + same-thread delivery (prevents one
  utterance firing multiple jumps and a `state_` data race). Grammar matching is phrase-level
  (audience speech cannot false-trigger a command) and logs no heard text (Bible §8). Findings:
  `docs/security-audits/f2-f3-voice-commands-security-audit.md` (2-agent adversarial audit).
- Deck loader (F1a) hardened against hostile .pptx input: ZIP64 size-cap wrap bypass closed
  (unsigned comparison), XML descendant walk made iterative to prevent stack-overflow DoS,
  per-slide shape cap added to prevent parse-time memory exhaustion. Findings + remediation:
  `docs/security-audits/f1a-deck-loader-security-audit.md` (5-agent audit).
- Renderer (F1b) hardened: font-size clamp (prevents a giant-glyph hang), image decode
  allow-listed to PNG/JPEG with dimension + allocation caps (excludes CVE-prone TIFF/WebP/GIF
  codecs), clip rect (no letterbox bleed), and per-text-box paragraph/run/text caps. Findings:
  `docs/security-audits/f1b-slide-renderer-security-audit.md` (2-agent audit).

### Data Model
- Added the in-memory slide model (`src/model/slide_model.hpp`): Presentation → Slide →
  ShapeElement (TextBox | Image | Unsupported) / Background / LoadWarning. `ImageElement` now
  carries raw image bytes; `UnsupportedElement` carries geometry for placeholder rendering.
  No persisted schema (standalone app).

### Added
- **Feature F7b — Usable Presenter**: the app now actually presents. `File`/CLI opens a `.pptx`
  **off the UI thread**, every slide is **pre-rendered off-thread** before the talk (TM-018), and the
  deck is shown fullscreen on the external display with keyboard navigation, a privacy blackout
  (Esc), and a deliberate two-step quit. New modules under `src/present/` (display geometry, key
  translation, deck-load worker, pre-render worker) and `src/ui/` (slide surface, notice strip,
  presentation window, app shell). A second test binary (`pptv_ui_tests`) tests the widget layer.
- **Feature F7a — Presentation Funnel**: `PresentationController`, the single place in the product
  that computes a slide index, so both input paths (voice, keyboard) are range-checked once and it
  cannot be bypassed. Rejects out-of-range slide numbers rather than clamping (BUG-16); a
  quit-confirm state machine in which quitting is unreachable from any command; a privacy blackout
  (holding screen); and a CLOSED notice vocabulary (an id plus two ints, never free text) so deck
  content has no channel to a display or a log. See `src/present/`.
- **Feature F2/F3 — Voice-Command Grammar & Dispatch**: `matchCommand()` maps a recognized
  phrase to one of the five closed-grammar commands (next/previous slide, pause/continue
  presentation, go to slide N) or nothing — phrase-level matching so audience speech never
  false-triggers. `RecognizerController` dispatches commands through an Active/Paused listening
  gate: while paused, navigation is ignored (Q&A protection) and only "continue presentation"
  resumes. Pure and unit-tested; the speech engine plugs in behind the `IRecognizer` interface
  (a follow-on feature). See `docs/api and interfaces/voice-commands.md`.
- **Feature F4 — Slide-Number Parser**: `parseSlideNumber()` turns "go to slide N" text
  (digits, number words, digit-by-digit) into an integer; fails safe (nullopt) on garbage,
  malformed sequences, or overflow so a mis-heard command never jumps to the wrong slide.
- **Feature F1a — Deck Loader**: `DeckLoader::load()` parses an untrusted .pptx (libzip +
  pugixml) into the slide model — text runs with font/size/weight/color, EMU positions, solid
  backgrounds, resolved image references + bytes, and warnings for unsupported elements. Resource
  caps enforced (file size, slide count, per-part/cumulative decompression, per-slide shapes,
  per-box paragraphs/runs/text).
- **Feature F1b — Slide Renderer**: `SlideRenderer::render()` paints a slide to a QImage —
  backgrounds, positioned text with font/size/weight/color, images, letterboxing, and visible
  placeholders for unsupported elements and missing/disallowed images. Pure and deterministic.

### Changed
### Fixed
- A missing/unresolvable slide no longer silently drops (which drifted "go to slide N" onto the
  wrong slide); a placeholder slide preserves numbering and a warning is surfaced (audit F1a-3).

### Removed
### Infrastructure
- CI + local build gain libzip and pugixml (system packages; `.github/ci-deps-apt.txt`).

### Documentation
- ADR-0001 (Qt6 + Vosk architecture), Phase 1 threat model / data model / UI scaffold,
  Project Bible (16 §), and the F1a security-audit findings.
