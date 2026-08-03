# Interface: SlideRenderer (Feature F1b)

**Header:** `src/render/slide_renderer.hpp` · **Model:** `src/model/slide_model.hpp`

## Purpose

Render a parsed `Slide` to a pixel image for display. A **pure, deterministic, headless**
function — no file/zip access, no threading, no display server (it needs a `QGuiApplication`
for the font database, which the app already has). The off-thread pre-render orchestration at
deck-load (Bible §3 / TM-018) is the presentation controller's responsibility, not this
function's.

## Entry points

```cpp
static QImage SlideRenderer::render(const Slide& slide, Emu slideWidthEmu, Emu slideHeightEmu,
                                    int targetW, int targetH);
static QImage SlideRenderer::render(const Presentation& pres, int slideIndex,
                                    int targetW, int targetH);
```

## Rendering contract

- The slide is scaled **uniformly** (aspect from `slideWidthEmu:slideHeightEmu`) and centered
  in `targetW × targetH`, with **black letterbox bars** outside it. All painting is clipped to
  the slide rect — nothing bleeds into the bars.
- **Background:** solid color as declared; an absent background renders **white** (PowerPoint's
  default), never black. Background pictures are not rendered in the text tier (white fill).
- **Text:** each paragraph is drawn with its first run's family/size/weight/italic/color at its
  EMU position. Unspecified color defaults to black. (Mixed-format runs within one box use the
  first run — a text-tier MVP limitation.)
- **Images:** decoded only if PNG/JPEG within size bounds; any other format, a decode failure,
  or missing bytes renders a visible **"missing image"** placeholder.
- **Unsupported elements** (table/chart/SmartArt) render a visible labeled placeholder box at
  their position — never silently blank.
- **Placeholder slides** (`slide.placeholder`) render a dark "Slide unavailable" surface.
- A non-positive slide size or target size returns a safe neutral image (no divide-by-zero,
  no crash).

## Safety bounds (security audit F1b)

- Font pixel size is clamped to the slide height before use (no giant-glyph hang).
- Image decode is allow-listed to PNG/JPEG, capped at 40 Mpx and a 128 MiB allocation limit.
- Text volume is bounded upstream by the loader's paragraph/run/text caps.

## Determinism

Byte-identical output for identical input **on the same machine**. Not guaranteed across
machines (font substitution differs) — relevant only to a future shared render cache.

## Consumers

The presentation controller (F7) calls this to pre-render slides (off the UI thread) into a
cache, then blits the current slide to the projector.
