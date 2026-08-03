# Data Model — powerpoint-voice

<!--
  Phase 1 Step 1.4 output (checklist item: data_model_defined).
  Authority: ADR-0001 (architecture), docs/phase-0/data-contract.md (binding data
  rules incl. post-review amendment), PRODUCT_MANIFESTO.md §4 (data contracts) and
  §8 (Q1-Q13 resolutions), PROJECT_INTAKE.md §5.1/§5.1.1 (sensitivity taxonomy).
  Companion: docs/phase-1/threat-model.md (Step 1.3).

  This is a SPECIFICATION of types, fields, relationships, validation, and
  sensitivity — not code. It makes no technology choices beyond ADR-0001.
-->

**Date:** 2026-08-03
**Status:** Accepted (Phase 1 Step 1.4)
**Storage posture:** Standalone desktop app. **No database, no server, no ORM.** The
data model is (1) the in-memory domain model the from-scratch renderer builds from a
.pptx, and (2) one small persisted JSON settings file. There are **no migrations in
the SQL sense**; the only versioned-migration obligation in this project attaches to
the settings JSON schema (§4.2). The CLAUDE.md rule "all data model changes go through
versioned migrations" maps here to: *any settings-schema change requires a
`schema_version` bump, a migration step entry in §4.2, and an update to this document.*

---

## 1. In-Memory Domain Model (RenderModel)

### 1.1 The RenderModel / raw-OOXML boundary

The parser is the **only** component that touches raw OOXML (zip parts, XML DOM,
relationship files). It consumes the validated package (data-contract Transformation 1)
and emits a fully-resolved, immutable **RenderModel**. Everything downstream — renderer,
overlay, command dispatch, warning UI — reads the RenderModel and never the raw package.

Boundary rules (binding):

- All inheritance is resolved **at parse time**: slide → layout → master chains
  (backgrounds, text defaults), theme color references, relationship IDs → part names.
  The RenderModel contains no rIds, no theme indirection, no unresolved references.
- All geometry in the RenderModel stays **EMU-native** (§1.6). Pixels exist only at
  render time, per target display.
- Raw package data (zip handle, XML DOMs, undecoded media bytes) is released as soon
  as the RenderModel is built. Decoded pixels and embedded font bytes live only inside
  the RenderModel (memory-only — data-contract Confidential rules, G-6/Q9: no on-disk
  temp extraction, ever).
- Anything the parser cannot map into the entities below becomes an
  `UnsupportedElementPlaceholder` (§1.4) — **never** a silent drop or a wrong render
  (F1, §11-1).

### 1.2 Entity relationship overview

```
Presentation 1 ──▶ 1..300  Slide (ordered)
Slide        1 ──▶ 1       SlideBackground (variant)
Slide        1 ──▶ 0..N    ShapeElement (ordered by z_order; variant)
ShapeElement          =    TextBox | Image | UnsupportedElementPlaceholder
TextBox      1 ──▶ 0..N    Paragraph (ordered)
Paragraph    1 ──▶ 0..N    TextRun (ordered)
TextRun      N ──▶ 1       FontRef ──▶ FontResolution (per-deck font map)
Presentation 1 ──▶ 0..N    LoadWarning (the F1 triage list)
Presentation 1 ──▶ 0..N    EmbeddedFont (memory-only)
```

### 1.3 Entities

#### Presentation

| Field | Type | Validation / Notes | Sensitivity |
|---|---|---|---|
| `source_path` | path string | The opened .pptx path; used for display and the recent-files update only | Internal |
| `slide_size_emu` | `{cx, cy}` EMU | From `p:sldSz`; both > 0; if absent, OOXML default 9,144,000 × 6,858,000 (4:3) is applied **and** a LoadWarning is recorded | Public (dimensions only) |
| `slides` | ordered `Slide[]` | 1–300 entries (F1 cap; over-limit rejected at validation, before this model exists) | Confidential (aggregate) |
| `warnings` | ordered `LoadWarning[]` | The load-time triage list (§1.4); grouped by slide index for F1's "triage-shaped" display | Confidential-derived |
| `font_map` | `FontResolution[]` | One row per distinct requested family (§1.5) | Confidential-derived (family names from deck) |
| `embedded_fonts` | `EmbeddedFont[]` | §1.5; memory-only, never persisted | Confidential |
| `slide_count` | derived int | `slides.length`; the bounds authority for "go to slide N" | Public |

#### Slide

| Field | Type | Validation / Notes |
|---|---|---|
| `index` | int | 1-based, contiguous, matches `p:sldIdLst` order |
| `source_part` | string | e.g. `ppt/slides/slide3.xml` — retained solely so errors/warnings can *name the failing part* (F1); never displayed with deck text content |
| `background` | `SlideBackground` | Always present after inheritance resolution (§1.3 SlideBackground) |
| `shapes` | ordered `ShapeElement[]` | Document order of `p:spTree` = paint order; `z_order` is the position in this list |

#### SlideBackground (variant)

Resolved at parse time through the slide → layout → master chain.

| Variant | Fields | Validation / Fallback |
|---|---|---|
| `SolidColor` | `color: RGBA` | Theme refs resolved to concrete RGBA at parse |
| `Picture` | `image: Image` (fill mode: stretch-to-slide) | Tile/stretch-offset modes → render as stretch + LoadWarning (`UNSUPPORTED_FEATURE`) |
| *(fallback)* | — | Gradient, pattern, or unresolvable fills → `SolidColor{white}` + LoadWarning (`UNSUPPORTED_FILL`) — never silently wrong |

#### ShapeElement (common fields of all three variants)

| Field | Type | Validation / Notes |
|---|---|---|
| `shape_id` | int | From `nvSpPr`; uniqueness not assumed (warning if duplicated) |
| `shape_name` | string | Deck-authored name; may be empty. **Confidential-derived** — appears in the warning list, never in logs |
| `frame` | `RectEMU {x, y, cx, cy}` | From `a:xfrm`; `cx, cy ≥ 0`; missing xfrm on a placeholder-derived shape → inherited from layout/master at parse |
| `z_order` | int | Position in `spTree` document order |
| `rotation / flip` | captured raw | Non-identity transform is **not rendered** in MVP: shape renders unrotated **and** a LoadWarning (`UNSUPPORTED_FEATURE`, naming the shape) is recorded |

#### TextBox (ShapeElement variant)

| Field | Type | Validation / Defaults |
|---|---|---|
| `paragraphs` | ordered `Paragraph[]` | May be empty (empty text box renders nothing, no warning) |
| `vertical_anchor` | `top \| middle \| bottom` | Default `top` |
| `word_wrap` | bool | Default `true` |

#### Paragraph

| Field | Type | Validation / Defaults |
|---|---|---|
| `runs` | ordered `TextRun[]` | Empty paragraph = blank line |
| `alignment` | `left \| center \| right \| justify` | Default `left` |
| `indent_level` | int 0–8 | Default 0 |
| `bullet` | `none \| char {marker: string}` | Default `none`; auto-numbered and picture bullets → rendered as `char{"•"}` + LoadWarning (`UNSUPPORTED_FEATURE`) |
| `line_spacing_pct` | int | Default 100; range-checked 25–500, out-of-range clamps + warning |

#### TextRun — the atomic formatted-text unit

| Field | Type | Validation / Defaults |
|---|---|---|
| `text` | UTF-8 string | Deck content — **Confidential**; never logged, never persisted |
| `font` | `FontRef` (family name + requested bold/italic for matching) | Resolved via `font_map` (§1.5) |
| `size_pt` | float | From OOXML `sz` (hundredths of a point) ÷ 100; range 1–4000; absent → inherited default resolved at parse (ultimately 18 pt) |
| `weight` | `normal \| bold` | Default `normal` |
| `style` | `{italic: bool, underline: bool, strikethrough: bool}` | Defaults false |
| `color` | RGBA | Theme/scheme refs resolved at parse; unresolvable → black + LoadWarning |

#### Image (ShapeElement variant)

| Field | Type | Validation / Notes |
|---|---|---|
| `source_part` | string | e.g. `ppt/media/image4.png` — for warning/error naming only |
| `format` | `png \| jpeg \| gif \| bmp \| tiff` | Any other media format (EMF/WMF vector, video, audio posters) → the shape becomes an `UnsupportedElementPlaceholder` instead |
| `pixels` | decoded bitmap (memory-only) | Decode failure → placeholder + LoadWarning (`UNPARSEABLE_PART`, naming the part). Per-part decompressed cap enforced before decode (Q9) |
| `natural_size_px` | `{w, h}` | > 0 after successful decode |
| `frame`, `z_order` | inherited from ShapeElement | Image is scaled to `frame`; aspect handling is exactly what the deck specifies (frame is authoritative) |

### 1.4 UnsupportedElementPlaceholder — the data-driven F1 warning record

Every element the text+images tier cannot faithfully render becomes this record. The
F1 load-time warning list is **generated from these records** (plus FontSubstitution
rows) — the UI adds nothing; if it isn't in the model, it isn't in the list.

| Field | Type | Notes |
|---|---|---|
| `slide_index` | int | 1-based; primary grouping key for the triage list |
| `element_kind` | string | Qualified OOXML identity, e.g. `p:graphicFrame(chart)`, `a:videoFile`, `p:sp(rotated)` |
| `shape_name` | string | From the deck (Confidential-derived; display only) |
| `frame` | `RectEMU` \| `unknown` | Where the placeholder box is painted; `unknown` → full-slide-width notice row instead |
| `reason` | enum: `UNSUPPORTED_ELEMENT_TYPE` \| `UNSUPPORTED_FEATURE` \| `UNSUPPORTED_FILL` \| `UNSUPPORTED_IMAGE_FORMAT` \| `UNPARSEABLE_PART` | Machine key; drives triage grouping and test assertions |
| `detail` | string | Human message; **must name the part or tag; must never quote run text or other deck content beyond the element/shape name** |

Render behavior: a visible hatched placeholder box at `frame` (or a slide-level notice
when `frame` is unknown) — never a blank silent gap (§11-1).

`LoadWarning` (the list row type) is the variant:
`UnsupportedElement(ref)` | `FontSubstitution(ref §1.5)` | `DefaultApplied(detail)`
(the last covers benign parse fallbacks like a missing `p:sldSz`).

### 1.5 Fonts

| Type | Fields | Rules |
|---|---|---|
| `FontRef` | `family: string`, `want_bold`, `want_italic` | As requested by a TextRun |
| `EmbeddedFont` | `source_part`, `bytes` (memory-only), `typeface_name`, `usable: bool` | Deck content: **Confidential**; exists only inside the process; never written to disk (data-contract Input 9) |
| `FontResolution` | `requested_family` → `resolved: embedded \| system \| substituted {substitute_family}` | One per distinct family. Every `substituted` (and every unusable embedded font) **must** produce a `FontSubstitution` LoadWarning — never silent (§11-3) |

### 1.6 Geometry and EMU→pixel scaling

- Canonical units: **EMU**. Constants: `914,400 EMU = 1 inch`; `12,700 EMU = 1 pt`.
- The RenderModel never stores pixels. At render, per target display:
  `scale = min(view_px_w / slide_cx_emu, view_px_h / slide_cy_emu)` (uniform,
  aspect-preserving), with centering (letterbox) offsets; `view_px` includes the
  display's device-pixel-ratio (HiDPI). Display hot-plug (F7) re-derives scale only —
  the model is untouched, which is what preserves slide position on disconnect.
- Font sizes render at `size_pt × 12,700 × scale` device pixels — text and geometry
  share one scale factor, keeping layout pixel-stable across displays.

### 1.7 Command grammar as data

The grammar is a **static data table**, the single source of truth for: the
speech-engine's closed grammar construction (ADR-0001), keyboard parity (F6), overlay
echo text (F5), and tests. Amended grammar per Manifesto §8 Q1 (two-word phrases).

**CommandType (exactly five — closed set):**

| CommandType | Voice phrase (exact) | Default key binding |
|---|---|---|
| `NEXT_SLIDE` | "next slide" | Right arrow / Space |
| `PREVIOUS_SLIDE` | "previous slide" | Left arrow |
| `PAUSE` | "pause presentation" | P (toggle) |
| `CONTINUE` | "continue presentation" | P (toggle) |
| `GOTO_SLIDE` | "go to slide ⟨number⟩" | typed digits + Enter |

Grammar vocabulary = the words of the five phrases + the number vocabulary
(digit words "zero"–"nine", number words for 1–300, and digit strings). Nothing else
is matchable (closed grammar — the false-trigger mitigation).

**NumberParseResult (variant)** — output of number normalization (data-contract
Transformation 7; exactly the three accepted forms per G-7):

| Variant | Fields | Produced by |
|---|---|---|
| `Parsed` | `value: int`, `form: DIGITS \| NUMBER_WORD \| DIGIT_BY_DIGIT` | "15" / "fifteen" / "one five" |
| `Unparseable` | `raw_text: string` (Confidential-derived, overlay-only) | Any other form, incl. mixed forms ("one fifteen") — overlay shows heard text, no movement |

Bounds checking (`1 ≤ value ≤ slide_count`) is **not** part of parsing — it happens in
dispatch, identically for voice and typed numbers (F6).

**Command record** (what dispatch consumes — voice and keyboard produce the same type):

`{ type: CommandType, target: int? (GOTO_SLIDE only, pre-bounds-check),
   source: VOICE | KEYBOARD, confidence: float? (VOICE only), timestamp }`

### 1.8 Session state (memory-only)

`PresentationState`: `{ current_slide: int (1-based), paused: bool,
listening_state: LISTENING | PAUSED | ENGINE_DEAD, target_display_id }`.
No blank flag — B/blank dropped from MVP (Q2; supersedes the data-contract state row).
`OverlayHistory`: session-only ring of `{heard_text, matched: CommandType | NO_MATCH,
timestamp}` — Confidential-derived, memory-only, destroyed at session end.

---

## 2. Persisted State

The application persists **one** schema-versioned file: `settings.json` (UTF-8, local
config directory). `recent_files` is a field of this file. *Deviation note:* the data
contract's State Boundaries wording describes "two small local files (settings, recent
paths)"; this model consolidates them into one schema-versioned file to give the
recent-files list the same versioning/fallback protections — flagged for Orchestrator
ratification (§2.4). The opt-in rehearsal log (Q7) and opt-in debug log are
user-managed output files, not schema-versioned state (§2.5).

### 2.1 settings.json schema (schema_version 1)

| Key | Type | Default | Validation |
|---|---|---|---|
| `schema_version` | int ≥ 1 | *(required)* | Missing or non-integer → file treated as malformed (§2.3) |
| `overlay_fade_ms` | int | `3000` | Range 500–10,000; out-of-range → default + named warning (F5 ~3 s fade) |
| `mic_device_id` | string \| null | `null` | `null` = system default input (G-8). Field reserved now so the post-MVP device picker is a value change, not a schema change |
| `keybindings` | map: command-id → key name | the §1.7 defaults | Keys must be recognized key names; the map **must keep all five CommandTypes bound** — a binding-removal or collision that unbinds a Must-Have command rejects the map (defaults restored + named warning; data-contract Input 3) |
| `rehearsal_log_enabled` | bool | `false` | Opt-in only (Q7); enabling is an explicit user action |
| `recent_files` | array of path strings | `[]` | Max **10** entries (G-9), most-recent first, deduplicated; **paths only, never deck content**; non-string entries dropped with warning |

**Unknown keys are rejected with a named-key warning** (data-contract Input 3) — they
are not preserved, not round-tripped.

### 2.2 Read/write rules

- Read once at startup; applied on change; written only on user-initiated change
  (settings edit, deck open updating `recent_files`).
- Write failure is non-fatal: warning surfaced, presenting continues (Transformation 11).
- Writes are atomic (write-temp-then-rename) so a crash mid-write cannot corrupt the
  only persisted file.

### 2.3 Forward/backward version rule (never crash)

| Condition on load | Behavior |
|---|---|
| `schema_version == CURRENT` | Validate keys per §2.1; per-key violations → that key's default + named warning |
| `schema_version < CURRENT` (older file) | Apply ordered forward-migration steps (v1→v2→…), each defined as data in §4.2 (renames, added defaults); then validate as current. Migrated content is written back only on the next user-initiated change, after the §4.2 backup |
| `schema_version > CURRENT` (file from a newer app) | Do **not** interpret any keys. Load pure defaults + visible notice ("settings were saved by a newer version and were not loaded"). **Never modify or overwrite the file** — the newer app's state is preserved intact |
| Malformed JSON / missing / unreadable / bad `schema_version` | Load defaults + visible warning; never crash; never overwrite the bad file without explicit user action (G-3) |

### 2.4 recent_files caveat (Confidential-adjacent paths)

Paths are classified **Internal**, but file names can hint at the Confidential deck's
subject (data-contract Input 6/8 rationale). Rules: paths only, never content or
thumbnails; bounded at 10; stale paths fail gracefully on open (clear error, optional
prune — G-9); list is user-clearable; paths never appear in logs (the debug log logs
command events only). Consolidation into settings.json (§2 deviation note) is flagged
for Orchestrator ratification since the data contract enumerates two files.

### 2.5 Opt-in output files (not schema-versioned state)

| File | Gate | Content rule | Sensitivity |
|---|---|---|---|
| Rehearsal log | `rehearsal_log_enabled == true` (default false) | `{timestamp, heard_text, matched \| NO_MATCH, confidence, latency_ms}` — the **only** place heard text may ever persist (Q7); local, user-deletable | **Confidential** |
| Debug session log | Explicit enable, default off | Command IDs, timestamps, confidence, latency, correlation IDs **only** — never heard text, never deck content, never paths (Q7/G-4) | Internal |

---

## 3. Data Isolation / Access Control

Single-user local app: no accounts, no roles, no multi-tenancy. The isolation that
matters is **Confidential-data containment** (Intake §5.1.1 taxonomy; project
classification `confidential`). Binding rules from the data contract: deck content and
audio never persisted, never transmitted, never in logs of any kind, never in crash
dumps (Q8: no default core dumps; crash handling must not write process memory);
embedded fonts memory-only; no on-disk temp extraction that outlives the process (G-6).

| Data element (this model) | §5.1.1 class | Containment |
|---|---|---|
| `TextRun.text`, decoded `Image.pixels`, `EmbeddedFont.bytes`, `SlideBackground` picture | **Confidential** | RenderModel memory only; destroyed at deck close; never on disk, never in logs/crash artifacts |
| `shape_name`, `element_kind` + `detail` in warnings, `font_map` family names | **Confidential-derived** | Display only (F1 warning UI); session-lifetime; never logged |
| Microphone audio frames | **Confidential** | Not modeled here — buffers discarded immediately post-recognition; zero retention |
| `Unparseable.raw_text`, `OverlayHistory.heard_text` | **Confidential-derived** | Overlay + memory ring only; persists only via the opt-in rehearsal log (Q7) |
| Rehearsal log file (opt-in) | **Confidential** | Local, default-off, user-deletable |
| `settings.json` (incl. `recent_files`, keybindings, `mic_device_id`) | **Internal** | Local filesystem ACLs (single user); no secrets exist in this app |
| Debug session log (opt-in) | **Internal** | Hard content rule: command events only (G-4/Q7) |
| `Command`, `NumberParseResult.Parsed`, `PresentationState` | **Internal** | Memory-only; loggable to debug log (IDs/values, no heard text) |
| Grammar/phrase table, EMU constants, `slide_size_emu`, `slide_count` | **Public** | Static app data / non-sensitive metadata |
| Bundled speech-model files | **Public** | Read-only app asset; license verification is a Phase 1 obligation (Q12) |

---

## 4. Lifecycle

### 4.1 Ephemeral vs. persistent

| Data | Created | Destroyed | Persistence |
|---|---|---|---|
| RenderModel (slides, text, pixels, fonts, warnings, font map) | At deck open — **re-read from the .pptx on every open**, no caching | Deck close / app exit | Memory only |
| Raw OOXML package (zip, DOMs, undecoded media) | During parse | Released immediately after RenderModel construction | Memory only |
| Audio buffers | During capture | Immediately after each recognition pass | Memory only |
| `PresentationState`, `OverlayHistory` | Presentation start | Session end (survives display hot-plug) | Memory only |
| `settings.json` (incl. `recent_files`) | First save | User deletes | Disk (local JSON) |
| Rehearsal log / debug log | Only when explicitly enabled | User-managed | Disk (local, opt-in) |
| Speech-model files | App install | App uninstall | Disk (read-only asset) |

### 4.2 Create / migrate / rollback — settings.json (the only versioned schema)

- **Create:** first write produces a complete `schema_version: CURRENT` file with all
  §2.1 defaults, atomically (§2.2).
- **Forward migration:** each schema bump vN→vN+1 registers an ordered migration step
  (declared as data in this section's table below; currently empty at v1). Steps may
  only rename keys, add keys with defaults, or tighten validation with
  default-fallback — never a step that can fail hard.
- **Backup-before-migrate:** before the first write-back of migrated content, the old
  file is preserved as `settings.v{N}.bak` (one backup retained). This is the
  mechanical rollback artifact.
- **Rollback = safe-load fallback:** any load failure at any stage degrades to
  in-memory defaults with a visible notice and leaves the on-disk file untouched
  (§2.3) — so reverting the app version, restoring the `.bak`, or deleting the file
  always yields a working app. There is no state a bad settings file can put the app
  in that requires manual repair. Newer-version files are never rewritten (§2.3),
  so app-version rollback after an OS-level app upgrade is also safe.

| Migration | Steps |
|---|---|
| *(v1 is current — table intentionally empty)* | — |

---

## Deviations / flags for Orchestrator

1. **Single persisted file:** `recent_files` lives inside `settings.json` (one
   schema-versioned file) rather than the data contract's "two small local files"
   wording — same data, same rules (paths only, max 10), better fallback protection.
   Ratify or split back into two files at Phase 2 init.
2. **Blank flag removed** from `PresentationState` relative to the data contract's
   State Boundaries row, per the binding post-review amendment (Q2).
3. **`mic_device_id` reserved at MVP** (always `null`/system-default per G-8) so the
   post-MVP mic picker needs no schema bump.
