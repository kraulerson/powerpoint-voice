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

## Feature F1b: Slide Renderer

**Phase Built:** 2
**Status:** Complete
**Summary:** Renders a parsed slide to a pixel image (`SlideRenderer::render` → QImage) — the
second half of MVP feature F1. Draws solid backgrounds, text with declared font/size/weight/
color at the correct EMU positions, embedded images, and VISIBLE placeholder boxes for
unsupported elements and missing/disallowed images. Scales the slide uniformly with black
letterbox bars and clips all content to the slide. A pure, deterministic, headless function
(no file/thread/display) — the off-thread pre-render orchestration is F7's job.
**Key Interfaces:** `src/render/slide_renderer.hpp` (`SlideRenderer::render`); see
`docs/api and interfaces/slide-renderer.md`. Also extends F1a's model with `ImageElement.imageData`
and `UnsupportedElement`, and the loader to load image bytes + record positioned placeholders.
**Related ADRs:** ADR-0001 (QPainter/QImage rendering choice).
**Test Coverage:** Unit + pixel tests — 23 renderer cases (`tests/test_slide_renderer.cpp`):
backgrounds, text color/size/position, images, letterbox, placeholders, Karl's 7 orchestrator
assertions, and 4 security-audit regressions. Runs under a headless QGuiApplication (offscreen).
**Security:** `docs/security-audits/f1b-slide-renderer-security-audit.md` — 2-agent audit; 1
Critical (font-size hang) + 2 High (untrusted image-codec allow-list, unbounded text) fixed
test-first, plus clip/pixel/size hardening.
**Known Limitations:** A text box with mixed-format runs renders with the first run's font
(single-run is the common case). Background PICTURES aren't rendered (fill white). Cross-machine
pixel-determinism is not guaranteed (font substitution) — relevant only to a future shared cache.

---

<!-- Copy the section above for each new feature. Number sequentially. -->
