> **POST-REVIEW AMENDMENT (2026-08-03, binding):** At Phase 0 review Karl resolved all flagged questions and CHANGED THE COMMAND GRAMMAR to two-word phrases: "next slide", "previous slide", "pause presentation", "continue presentation" (plus "go to slide N" unchanged). Bare single-word commands in this document are superseded. The B key / blank-screen is DROPPED from MVP (strict five-command parity). Authoritative resolutions: PRODUCT_MANIFESTO.md §8 (Q1-Q13).
# Data Contract — powerpoint-voice

<!--
  Phase 0 Step 0.3 output. This document captures the full data input/output
  specification before it is summarized into the Product Manifesto Section 4.

  Source of authority: PROJECT_INTAKE.md §5 (Data & Integrations), §4.1 (features),
  §2.3 (success criteria / latency), §5.1.1 (classification), §11 (known risks).

  This defines WHAT data flows through the system, not HOW (architecture is Phase 1).
  No technology, library, or engine choices are made here.
-->

**Date:** 2026-08-03
**Status:** Draft

---

## Data Inputs

Inputs 1–3 are declared in Intake §5.1. Inputs 4–9 are **implied by Must-Have
features but absent from the §5.1 table** — each cites the feature that requires it
(see Gaps & Recommendations, G-1).

| # | Input | Source | Format | Validation Rules | Sensitivity |
|---|-------|--------|--------|-----------------|-------------|
| 1 | .pptx presentation file | User (File→Open, drag-and-drop, or CLI argument — §4.1-1) | Binary OOXML package (zip container) | Required to present. Must be a readable, valid zip; must contain `[Content_Types].xml` and `ppt/presentation.xml`; file size ≤200 MB; slide count ≤300 (reject over-limit with the stated limit named); declared decompressed size must be bounded before extraction (zip-bomb defense — see G-6); every unsupported slide element must be surfaced at load in a warning list and rendered as a visible placeholder, never silently wrong; corrupt/invalid file → error naming the failing part, app keeps running (§4.1-1) | **Confidential** — executive deck content; never leaves the machine, never committed (fixtures are synthetic/sanitized), never written to logs or crash dumps |
| 2 | Microphone audio | System default audio input device (live room capture) | PCM stream, ~16 kHz, mono | Required for voice control (keyboard path must work without it). Device presence checked at start and continuously; device unavailable or hot-unplugged → persistent on-screen banner, keyboard fallback unaffected (§4.1-2); processed on-device, in memory only; never persisted, never transmitted (§4.3-5) | **Confidential** — live room audio; in-memory only, zero retention |
| 3 | User settings | Local JSON preferences file | JSON (UTF-8) | Optional (app runs on defaults). Schema-validated keys only: overlay fade duration, microphone device, keybindings; unknown keys rejected with a named-key warning; malformed/unreadable file → fall back to defaults with a visible warning, never crash (see G-3); values range-checked (e.g., fade duration within sane bounds; keybindings must not unbind a Must-Have command) | **Internal** |
| 4 | Offline speech-recognition model files | Application package (build/packaging-time input; read-only at runtime) | Model/grammar data files bundled inside the installed app | Implied by §6.4 hard constraint (fully offline, "including speech recognition") + §4.1-2/3/4. Must ship inside the application package — never downloaded at runtime. Presence and integrity verified at startup; missing/corrupt → treat as "mic unavailable": persistent banner + keyboard fallback (§4.1-2 failure state); redistribution license verified in Phase 1 | **Public** (redistributable asset; contains no user data) — license check pending (G-2) |
| 5 | Keyboard events | User via OS input events | Key codes / typed characters | Implied by §4.1-6. Only mapped keys act (arrows, space, B, P, digits+Enter); typed-number+Enter goes through the identical bounds check as voice "go to slide N"; keyboard path must be fully independent of the speech engine — operable when the engine is dead or paused | **Public** (transient control input) |
| 6 | CLI argument (deck path) | User via command line at launch | Filesystem path string | Implied by §4.1-1 ("or CLI argument"). Path must exist, be readable, and pass the identical .pptx pipeline validation as Input 1; unknown flags rejected with usage text; at most one deck path accepted | **Internal** (file paths/names may hint at confidential deck subject) |
| 7 | Display topology & hot-plug events | Operating system display subsystem | System events (display attach/detach, resolution) | Implied by §4.1-7. External display attached → slide view routes to it; no external display → single-screen full-screen; disconnect mid-show → fall back to laptop screen without crashing or losing the current slide position | **Public** (system metadata) |
| 8 | Recent-files list | Local persisted file (paths only) — read at startup | List of filesystem path strings | Implied by §5.4 ("recent-files list (paths only, never deck content)"). Schema-valid list of strings; bounded entry count; entries whose path no longer exists fail gracefully on open (clear error, optional prune) and never crash; must never contain deck content | **Internal** (see #6 rationale) |
| 9 | Embedded fonts inside the .pptx | Sub-input of Input 1 (OOXML font parts) | Embedded font data within the package | Implied by Known Risk 3 (§11-3: deck fonts unknown). Parsed in memory as part of the deck; any substitution (embedded font unusable, or referenced font absent from the system) must be visibly reported at load — never silent (§11-3); font data is deck content: never persisted outside the running process | **Confidential** (part of the deck package) |

**Sensitivity classifications verified against Intake §5.1:** rows 1–3 confirmed as
recorded (Confidential / Confidential / Internal). No corrections to declared rows;
proposed classifications for the six implied inputs are new (Gaps G-1, G-2).

---

## Data Transformations

| # | Input(s) | Transformation | Output | Error Behavior |
|---|----------|---------------|--------|----------------|
| 1 | .pptx file (Input 1) | Container validation: zip integrity, required OOXML parts present, size/slide-count/decompressed-size limits enforced | Validated OOXML package handle | Reject with an error naming the failing part or the exceeded limit; app keeps running (§4.1-1) |
| 2 | Validated package | OOXML parse: presentation part, per-slide XML, relationships, media parts, embedded font parts → in-memory slide model (text runs with formatting, placed images, solid/picture backgrounds, slide order/count) | In-memory slide model (text+images tier only, §4.1-1) | Unparseable part → error naming the part; unsupported element → recorded in the load-time warning list and modeled as a placeholder (never silent drift, §11-1) |
| 3 | Slide model + embedded fonts (Input 9) | Font resolution: embedded → usable? referenced → available? else substitution mapping | Per-deck font map with an explicit substitution report | Every substitution surfaced visibly at load; never silent (§11-3) |
| 4 | Slide model + font map | Slide rendering: each slide composed to a pixel-stable full-screen raster/view | Rendered slide view (Output 1) | Render failure of one element → visible placeholder + warning; render failure of the view must not crash the app |
| 5 | Microphone PCM (Input 2) | On-device recognition, constrained to the fixed command grammar (§2.4-3): audio frames → candidate heard text + confidence | Heard-text hypothesis with confidence score | Engine crash → automatic engine restart with overlay alert (§4.1-3); keyboard path unaffected; audio buffers discarded immediately after processing |
| 6 | Heard-text hypothesis | Grammar match against exactly: "next", "previous", "pause", "continue", "go to slide \<number\>" | Matched command, or no-match | Low-confidence → no action; overlay shows what was heard (§4.1-2). No-match → overlay "no match" with heard text (§4.1-5) |
| 7 | Matched "go to slide" phrase | Number normalization: digits ("15"), number words ("fifteen"), digit-by-digit ("one five") → integer N; then bounds check 1 ≤ N ≤ deck slide count | Target slide index | Unparseable number → overlay shows heard text, no movement; N out of range → overlay "deck has M slides", no movement (§4.1-4) |
| 8 | Matched command (voice) or keyboard event (Input 5) | Command dispatch through a single shared action layer: next/previous (edge no-op + overlay notice at first/last slide), jump-to-N, pause/continue (idempotent; pause suspends all matching except "continue", §4.1-3), blank (B), and the keyboard equivalents | Slide-state mutation (current index, pause flag, blank flag) → re-render + overlay update | Voice and keyboard must produce identical actions (§4.1-6); dispatch while paused: only "continue" (or any keyboard key) acts |
| 9 | Every processed utterance | Overlay composition: heard text + matched command (or "no match") rendered to the reserved strip, auto-fading after ~3 s | Command-transcript overlay (Output 2) | Overlay rendering failure must never take down the slide view; overlay never exceeds its reserved strip (§4.1-5) |
| 10 | Settings JSON (Input 3) | Parse → schema validation → typed settings object applied at startup and on change | Active preferences | Unknown key → rejected + named warning; malformed file → defaults + visible warning (G-3) |
| 11 | Deck open event | Recent-files update: prepend opened path, dedupe, truncate to bounded length, write back (paths only) | Persisted recent-files list (Output 6) | Write failure → non-fatal warning; presenting continues |
| 12 | Display events (Input 7) | Output routing: choose target display for the slide view; on disconnect, re-route to laptop screen preserving slide position | Routed full-screen view | Mid-show disconnect must not crash or lose position (§4.1-7) |

---

## Data Outputs

Latency expectations from Intake §5.2 and §2.3. End-to-end voice budget: **≤1.5 s p95
from end of utterance to completed slide action** (§2.3), which contains the per-output
budgets below.

| # | Output | Destination | Format | Retention | Sensitivity |
|---|--------|-------------|--------|-----------|-------------|
| 1 | Rendered slide view | Screen (external display/projector when attached, else laptop) | Full-screen rendered slide + slide counter, minimal-dark UI (§4.1-7) | Ephemeral — display only, nothing written | **Confidential** content in-motion (deck pixels on screen only) |
| 2 | Command-transcript overlay | Screen (reserved strip of the presenting display) | Heard text + matched command / "no match"; auto-fade ~3 s; contrast ≥4.5:1 (§9); **<500 ms** after utterance (§5.2, §4.1-5) | Session-only (in-memory history); never persisted | **Confidential**-derived (text derived from live room audio); zero retention |
| 3 | Slide change (render latency) | Screen | New slide fully rendered **<200 ms** per slide change (§5.2); within the ≤1.5 s p95 end-to-end voice budget (§2.3) | Ephemeral | — (same content as Output 1) |
| 4 | Load-time warning report | Screen (dialog/panel at deck load) | List of every unsupported element and every font substitution (§4.1-1, §11-3) | Session-only | **Confidential**-derived (names/locations of deck elements); display only |
| 5 | Session command log | In-memory; optional local debug file, **off by default** (§5.2) | Timestamped structured entries: matched command, action taken, confidence, latency | Memory: session-only. Debug file (only when explicitly enabled): user-managed, local only | **Internal**, with a hard content rule: **must never contain deck content or audio**; inclusion of raw heard text needs an Orchestrator decision (G-4) |
| 6 | Recent-files list | Local persisted file | Bounded list of deck paths (paths only, never content — §5.4) | Until user deletes / rolls off the bounded list | **Internal** |
| 7 | User settings | Local JSON preferences file | Schema-valid JSON, written on change | Until the user deletes them (§5.4); no regulatory retention | **Internal** |
| 8 | Status banners / error surfaces | Screen | Mic-unavailable banner, engine-restart alert, PAUSED indicator, edge-of-deck notice — always paired text/icon/position, never color alone (§9) | Ephemeral | **Internal** |

---

## Third-Party Integrations

| # | Service | Data Sent | Data Received | Fallback if Unavailable |
|---|---------|-----------|---------------|------------------------|
| 1 | None — this product operates entirely offline/self-contained | N/A | N/A | N/A |

**None — fully offline by design** (Intake §5.3; §6.4 hard constraint). This forbids, at
runtime: cloud speech recognition, any telemetry or analytics, crash-report upload
services, update phone-home, remote font or asset fetching, runtime model downloads,
license-check callbacks, and any network I/O of any kind. The application must be 100%
functional with all networking disabled (§2.3 network-independence criterion). The
only network activity in the project's lifecycle is development-side (GitHub CI/
releases), which never touches user data.

---

## State Boundaries

Persistent state is limited to two small local files (settings, recent paths — §5.4,
<10 MB at 12 months). Everything touching Confidential data is ephemeral by design.

| Data | Lifecycle | Persistence | Backup Required |
|------|-----------|-------------|-----------------|
| Deck content: parsed slide model, image/media data, embedded fonts | Created at deck open → destroyed at deck close / app exit; re-read from the .pptx on every open (§5.4) | Memory only — never cached to disk | No (source .pptx is the user's own file, §5.4) |
| Microphone audio buffers | Created during capture → discarded immediately after the recognition pass | Memory only — never persisted, never transmitted (§4.3-5) | No |
| Transcript/overlay history | Created per utterance → destroyed at session end | Memory only (session-only, §5.4) | No |
| Presentation state (current slide index, pause flag, blank flag, display routing) | Created at presentation start → destroyed at session end; survives display hot-plug (§4.1-7) | Memory only | No |
| User settings | Created on first save → until user deletes (§5.4) | Disk (local JSON) | No |
| Recent-files list (paths only) | Updated on each deck open → until user deletes / rolls off | Disk (local file) | No |
| Optional debug session log | Only when explicitly enabled → user-managed | Disk (local, off by default) | No |
| Speech-recognition model files | Installed with the app → removed with the app | Disk (read-only application asset) | No |

**Confidential-data handling rules (binding on all later phases):**

- The .pptx content and microphone audio are Confidential and must **never appear in
  logs** of any kind — including the optional debug log and any structured error output.
- **Crash handling must not embed deck content or audio buffers** in crash dumps or OS
  crash reports; crash-handling behavior must be explicitly designed in Phase 1 (G-5).
- Deck content is never written to disk by the application (no thumbnail caches, no
  temp extraction of media/fonts that outlives the process — G-6 covers in-process
  temporary storage constraints).
- Real deck and real room audio are used exclusively in local UAT/rehearsal on Karl's
  machine; all committed fixtures are synthetic/sanitized (§5.1, §5.1.1 ZDR exception).

---

## Sensitivity Classification Summary

Taxonomy per Intake §5.1: Public, Internal, Confidential, PII, Financial,
Health/Medical, Regulated. Project-level `data_classification` = **`confidential`**
(highest row, §5.1.1) — confirmed correct by this contract; the implied inputs add no
higher class. ZDR gate: `zdr_attested=false` with a recorded CISO-approved exception
(§5.1.1) — the LLM sees only source code and synthetic fixtures, never the real deck.

| Classification | Data Items | Handling Requirements |
|---------------|------------|----------------------|
| **PII** | None (no accounts, no personal data collected — §6.4, §8.4) | N/A |
| **Confidential** | .pptx deck file; parsed slide model, media, embedded fonts; microphone audio; overlay heard-text (derived); load-warning element names (derived) | Never leaves the machine; never transmitted; never committed (synthetic fixtures only); never in logs or crash dumps; audio zero-retention; deck content memory-only, re-read per session |
| **Internal** | User settings JSON; recent-files list (paths); session command log / optional debug file; CLI-supplied deck path | Local-only; standard filesystem access control; debug file off by default and must never contain Confidential content |
| **Public** | Bundled speech-model files (license verification pending, G-2); keyboard events; display topology events | No restrictions beyond model license compliance |

---

## Gaps & Recommendations (FLAGGED FOR ORCHESTRATOR REVIEW)

Items found during Step 0.3 validation that the Intake does not decide. None add
features beyond §4.1; all are specification gaps. **G-1 is the headline finding.**

| ID | Gap | Recommendation |
|----|-----|----------------|
| G-1 | **Six inputs implied by Must-Have features are missing from Intake §5.1:** bundled speech-model files (build-time; §6.4 offline constraint), keyboard events (§4.1-6), CLI deck-path argument (§4.1-1), display topology/hot-plug events (§4.1-7), recent-files list as an input (§5.4 lists it only under persistence), and embedded fonts as a distinct sub-input (§11-3) | Adopt rows 4–9 of the Data Inputs table above into the project record; no Intake feature change required |
| G-2 | Speech-model files have no recorded classification or license status | Classify **Public**; verify the chosen model's redistribution license in Phase 1 before bundling |
| G-3 | Intake specifies "unknown keys rejected" for settings but not malformed-JSON behavior | Fall back to defaults with a visible warning; never crash; never overwrite the bad file without user action |
| G-4 | The optional debug log's permitted content is undefined; persisting raw heard text is in tension with §2.4-5 ("no transcription beyond the live command overlay") | Restrict the debug file to matched-command IDs, timestamps, confidence, and latency; require an explicit Orchestrator decision before any heard-text field is added |
| G-5 | Crash-dump/crash-report content policy is unstated; OS crash mechanisms can capture process memory containing deck content and audio | Make "crash artifacts must not embed Confidential data" an explicit Phase 1 design requirement |
| G-6 | §5.1's size limits do not bound decompressed size (zip-bomb risk) and don't state whether media/fonts may be temp-extracted to disk | Add a decompressed-size ceiling to load validation; require in-memory processing of deck parts (no on-disk temp extraction that outlives the process) |
| G-7 | Number grammar: mixed forms (e.g., "one fifteen") and forms like "slide fifteen" without "go to" are unspecified | Accept exactly the three §4.1-4 forms; everything else takes the unparseable path (overlay shows heard text, no movement) |
| G-8 | MVP has no mic-device picker (post-MVP §4.2-3), but default-device selection and mic hot-unplug behavior are needed at MVP | Use the system default input device; treat hot-unplug as "mic unavailable" (persistent banner + keyboard fallback, §4.1-2) |
| G-9 | Recent-files list bound and stale-path policy unspecified | Bound the list (suggest 10 entries); stale paths error gracefully on open and may be pruned; never store deck content |

No integration risk exists (no third parties). The material data risks are all
containment risks on the Confidential class (G-4, G-5, G-6).

---

## Review Checklist

- [x] Every input has a defined source and validation rules
- [x] Every transformation has an error/fallback behavior
- [x] Every output has a defined destination and retention policy
- [x] PII is identified and handling requirements are specified (none exists; confirmed)
- [x] Third-party integrations have fallback behavior defined (none — fully offline)
- [x] State boundaries are clear (ephemeral vs. persistent)
