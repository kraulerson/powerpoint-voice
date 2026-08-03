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
- Deck loader (F1a) hardened against hostile .pptx input: ZIP64 size-cap wrap bypass closed
  (unsigned comparison), XML descendant walk made iterative to prevent stack-overflow DoS,
  per-slide shape cap added to prevent parse-time memory exhaustion. Findings + remediation:
  `docs/security-audits/f1a-deck-loader-security-audit.md` (5-agent audit).

### Data Model
- Added the in-memory slide model (`src/model/slide_model.hpp`): Presentation → Slide →
  ShapeElement (TextBox | Image) / Background / LoadWarning. No persisted schema (standalone app).

### Added
- **Feature F1a — Deck Loader**: `DeckLoader::load()` parses an untrusted .pptx (libzip +
  pugixml) into the slide model — text runs with font/size/weight/color, EMU positions, solid
  backgrounds, resolved image references, and warnings for unsupported elements. Resource caps
  enforced (file size, slide count, per-part/cumulative decompression, per-slide shapes).

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
