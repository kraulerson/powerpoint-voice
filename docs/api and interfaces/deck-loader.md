# Interface: DeckLoader (Feature F1a)

**Header:** `src/loader/deck_loader.hpp` · **Model:** `src/model/slide_model.hpp`

## Purpose

Load and validate an untrusted `.pptx` into an in-memory `Presentation` for the renderer.
The input is attacker-controlled (arrives by email/USB), so validation and resource caps are
part of the contract, not an afterthought.

## Entry point

```cpp
static LoadResult DeckLoader::load(const QString& path,
                                   const LoaderLimits& limits = LoaderLimits{});
```

- **Never writes to disk** — the archive is opened read-only and parts are read in memory.
- **Never throws across the boundary** — every failure is a typed `LoadError`.
- **Deterministic** — output is a pure function of the file bytes and `limits`.

## Return contract — `LoadResult`

| Field | Meaning |
|---|---|
| `ok` | `true` on success (even with warnings); `false` on a hard validation failure |
| `error` | `LoadError { kind, message }` when `ok == false`. `message` names the failing part but **never contains deck content** |
| `presentation` | The parsed model when `ok == true` |

### `LoadErrorKind`

`FileNotFound`, `NotAZip`, `MissingPresentationPart`, `MalformedXml`, `FileTooLarge`,
`TooManySlides`, `PartTooLarge`, `DecompressionLimit`.

## `LoaderLimits` (resource caps — all injectable for testing)

| Field | Default | Guards |
|---|---|---|
| `maxFileBytes` | 200 MB | oversized archive |
| `maxSlides` | 300 | deck length |
| `maxTotalUncompressed` | 1 GB | cumulative zip-bomb |
| `maxPartUncompressed` | 128 MB | single-part bomb |
| `maxShapesPerSlide` | 5000 | per-slide shape flood |

## Model highlights

- `Presentation { slideWidth, slideHeight (EMU), slides[], warnings[] }`
- `Slide { background, elements[] (z-ordered), warnings[], placeholder }`
  - `placeholder == true` marks a slide whose part was missing/unresolvable. **The slide is
    kept (not dropped) so slide N always maps to `slides[N-1]`** — critical for "go to slide N".
- `ShapeElement { kind: TextBox | Image, textBox, image }`
- `TextBox { rect(EMU), paragraphs[] → runs[] { text, fontFamily, fontSizePt, bold, italic, color } }`
- `ImageElement { rect(EMU), mediaPart }` — the referenced media part path; **pixels are decoded
  by the render layer (F1b)**, not here.
- `LoadWarning { slideIndex, elementType, detail }` — one per unsupported element
  (table/chart/smartArt), per missing slide, per capped slide, or for a missing slide size.

## Guarantees for consumers

1. On `ok`, `slides.size()` equals the deck's declared slide count (placeholders fill gaps).
2. Slide/element order matches the deck's declared order.
3. Unsupported elements produce warnings + placeholders, never a wrong render or a failed load.
4. No deck text or image bytes appear in `LoadError.message`, logs, or on disk.

## Not covered here (later features)

Pixel rendering of slides (F1b), keyboard/voice navigation, the presentation UI.
