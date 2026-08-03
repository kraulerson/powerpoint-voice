# Feature Reference

<!--
  This document is a living index of all features built during Phase 2.
  Update at Step 2.5 of every Build Loop iteration alongside the CHANGELOG and Bible.
  Purpose: Give someone a quick orientation to what the app does without reading the Bible.
  For detailed analysis, follow the links to ADRs and interface docs.
-->

## Feature F1a: Deck Loader (PPTX parse layer)

**Phase Built:** 2
**Status:** Complete
**Summary:** Loads and validates an untrusted `.pptx` and builds the in-memory slide model
(Presentation → Slide → text boxes, images, backgrounds) that the renderer (F1b) will paint.
This is the first half of MVP feature F1. It parses OOXML text-tier content, resolves image
references, records warnings for unsupported elements (tables/charts/SmartArt) instead of
failing, and enforces resource caps against hostile decks (zip-bomb, deep nesting, shape
floods) while never writing to disk or logging deck content.
**Key Interfaces:** `src/loader/deck_loader.hpp` (`DeckLoader::load`), `src/model/slide_model.hpp`;
see `docs/api and interfaces/deck-loader.md`.
**Related ADRs:** `docs/ADR documentation/ADR-0001-architecture-qt6-vosk.md` (libzip + pugixml choice).
**Test Coverage:** Unit + integration — 17 deck-loader cases (`tests/test_deck_loader.cpp`):
happy paths, all error paths, security caps, Karl's 7 orchestrator assertions, and 4
security-audit regression tests. Fixtures are synthetic OOXML (`tests/fixtures/`), never the
real deck.
**Security:** `docs/security-audits/f1a-deck-loader-security-audit.md` — 5-agent audit; 3
Critical + 1 High found and fixed test-first; containment + resource-lifecycle audits clean.
**Known Limitations:** Text+images tier only (unsupported elements → warning/placeholder).
Image PIXELS are not decoded here (F1b). Malformed EMU/font-size attributes default to 0
(accepted). Slide rendering, keyboard/voice control, and the presentation UI are later features.

---

<!-- Copy the section above for each new feature. Number sequentially. -->
