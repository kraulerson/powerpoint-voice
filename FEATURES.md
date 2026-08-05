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

## Feature F4: Slide-Number Parser ("go to slide N")

**Phase Built:** 2
**Status:** Complete
**Summary:** `parseSlideNumber(text)` turns the spoken/typed number in a "go to slide N" command
into an integer — the correctness-critical piece behind jumping to the right slide. Handles
digits ("15"), number words ("fifteen", "twenty three", "one hundred twenty three"), and
digit-by-digit ("one five" → 15), strips "go to slide" filler, and is case/whitespace/hyphen
tolerant. Fails safe: garbage, malformed sequences ("fifteen fifteen"), and overflow return
nothing (no jump), so a mis-heard command never lands on the wrong slide.
**Key Interfaces:** `src/command/number_parser.hpp`; see `docs/api and interfaces/number-parser.md`.
**Test Coverage:** Unit — 20 cases (`tests/test_number_parser.cpp`): digits, words, hundreds,
digit-by-digit, filler, case/whitespace, unparseable, Karl's 7 orchestrator assertions, and 2
security-audit regressions (overflow/flood + malformed-sequence rejection).
**Security:** `docs/security-audits/f4-number-parser-security-audit.md` — 3 findings fixed
test-first (negative overflow, unchecked toInt, malformed-sequence wrong-jump).
**Known Limitations:** English only. Range-checking against the deck length (out-of-range →
"deck has N slides") is the caller's job (F2/F7), not the parser's.

---

## Feature F2/F3: Voice-Command Grammar & Dispatch

**Phase Built:** 2
**Status:** Complete (command logic; live voice input is the follow-on voice-engine feature)
**Summary:** The "brain" of voice control. `matchCommand(phrase)` maps a recognized/typed
phrase to one of the five closed-grammar commands — `next slide`, `previous slide`,
`pause presentation`, `continue presentation`, `go to slide N` — or nothing. Matching is
phrase-level (not substring) and case/whitespace/terminal-punctuation tolerant, so ordinary
speech that merely contains a keyword never fires a command (threat TM-002/019). The number in
"go to slide N" reuses F4's `parseSlideNumber`. `RecognizerController` dispatches commands
through an **Active/Paused** listening gate: while Paused (for audience Q&A), navigation is
dropped and only `continue presentation` resumes — the core false-trigger defense. The speech
engine + microphone plug in behind the `IRecognizer` interface and are built/validated
separately (UAT with real audio), keeping this layer pure and deterministic.
**Key Interfaces:** `src/command/command_matcher.hpp` (`matchCommand`, `Command`, `CommandType`),
`src/command/recognizer_controller.hpp` (`RecognizerController`, `IRecognizer`); see
`docs/api and interfaces/voice-commands.md`.
**Related ADRs:** ADR-0001 (Vosk grammar-constrained recognizer).
**Test Coverage:** Unit — 20 cases across `tests/test_command_matcher.cpp` and
`tests/test_recognizer_controller.cpp`: the five commands, case/whitespace/punctuation
tolerance, fail-safe garbage/partial/in-sentence/no-number rejection, GoToSlide boundary,
Karl's 3 orchestrator assertions, the full Active/Paused state machine (incl. the
"Paused ignores next slide" safety property), and 3 security-audit regressions
(punctuation availability, sink-exception safety, reentrancy).
**Security:** `docs/security-audits/f2-f3-voice-commands-security-audit.md` — 2-agent
adversarial audit; 1 High (sink-exception → mid-talk crash) + 1 Medium availability
(dictation punctuation) + 1 Medium (reentrancy) fixed test-first, plus threading/finals-only
lifetime contracts documented. Confirmed clean: no false-trigger leak, no heard-text logging.
**Known Limitations:** English only; strict fixed phrases (no filler tolerance on the four
non-jump commands, by design). This layer does not capture audio or run the recognizer — that
is the voice-engine feature (Vosk + miniaudio), which must honor the documented `IRecognizer`
contract (finalized-phrases-only, same-thread delivery).

---

## Feature F7a/F7b: Presentation UI (funnel + usable presenter)

**Phase Built:** 2
**Status:** Complete for the core presenter; later F7 sub-features add caps/cache, load report,
display routing polish, pre-show report, settings and accessibility.
**Summary:** The point at which powerpoint-voice became a product. **F7a** built
`PresentationController` — the single place in the product that computes a slide index, so both
input paths are range-checked once (BUG-16) and quitting is unreachable from any command. **F7b**
made it present: an untrusted `.pptx` is parsed **off the UI thread**, every slide is **pre-rendered
off-thread** before the talk (TM-018 — never lazily mid-presentation), and the deck is displayed
fullscreen on the external display with keyboard navigation, a privacy blackout, and a deliberate
two-step quit.
**Key Interfaces:** `src/present/` (presentation_controller, notice, display_geometry,
key_translator, deck_load_worker, pre_render_worker) and `src/ui/` (slide_surface, notice_strip,
presentation_window, app_shell).
**Test Coverage:** 178 tests, including a second binary (`pptv_ui_tests`, own `QApplication`) for the
widget layer. Highlights: a 40-dispatch matrix + 500-step fuzz proving quit is unreachable; rendering
**proven off-thread**; the render bomb proven never to reach the renderer; Esc proven not to close the
window.
**Security:** `docs/security-audits/f7a-presentation-funnel-security-audit.md` and
`f7b-usable-presenter-security-audit.md` — 2 High + 5 Critical + 6 High found and fixed test-first;
ThreadSanitizer clean.
**Known Limitations:** the TM-018 caps count shapes and text runs only (BUG-21) and the raster cache
is unbounded (BUG-22) — both land in F7c with the ratified four-cap set and 2 GB window. Voice is not
yet wired (the recognizer arrives with the voice-engine feature).

---

<!-- Copy the section above for each new feature. Number sequentially. -->

## F8a — Audio capture format conversion

Converts whatever the capture device provides into the 16 kHz mono the recogniser requires.
Pure functions, no hardware dependency — deliberately, because the talk runs on a MacBook Pro
M3 Max and the development machine has no microphone at all.

- Accepts 8-192 kHz, 1-64 channels; rejects anything else rather than converting it
- Multi-channel input is AVERAGED (a MacBook Pro's mic is a 3-element array), not channel-0 sampled
- Linear resampling, dependency-free
- Rate/channel bounds are a resource cap: a device claiming 1 Hz would otherwise turn one
  4096-frame callback into 131 MB (audit F8a-1)
- Performs no I/O of any kind, so audio cannot reach disk or a log (TM-011/012/013)
- `docs/api and interfaces/audio-capture-format.md`, `docs/security-audits/f8a-audio-capture-format-security-audit.md`
