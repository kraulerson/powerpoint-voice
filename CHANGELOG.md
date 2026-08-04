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
