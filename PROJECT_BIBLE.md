# Project Bible — powerpoint-voice

<!-- Last Updated: 2026-08-04 (Phase 2: F2/F3 voice-command grammar & dispatch built) -->

**Status:** Draft — pending Phase 1 → Phase 2 gate approval (Senior Technical Authority)
**Phase Gate:** Phase 1 → Phase 2
**Companion artifacts (authoritative sources, synthesized here):** `PRODUCT_MANIFESTO.md`,
`docs/ADR documentation/ADR-0001-architecture-qt6-vosk.md`, `docs/phase-1/threat-model.md`,
`docs/phase-1/data-model.md`, `docs/phase-1/ui-scaffolding.md`, `docs/phase-0/*`.

---

## 1. Product Manifesto

Full text: `PRODUCT_MANIFESTO.md` (approved by Sponsor 2026-08-03, Phase 0→1 gate).
Summary: a fully-offline C++ desktop app that controls a PowerPoint deck by voice
(five two-word commands + "go to slide N") with strict keyboard parity, a live transcript
overlay, and a minimal-dark UI, built for a live executive presentation ~2026-08-10.
MVP cutline = features F1–F7 (§5 of the Manifesto). Command grammar (Q1): "next slide",
"previous slide", "pause presentation", "continue presentation", "go to slide N".

## 2. Revenue Model & Cost Constraints

N/A — internal tool, no revenue (Manifesto Appendix A). Runtime cost $0 (fully local,
no hosting, no APIs). Infrastructure ceiling $0; CI on GitHub free tier (public repo).
Full track completed this section for consistency; unit economics cannot become
unsustainable at zero marginal cost.

## 3. Architecture Decision Record

Full ADR: `docs/ADR documentation/ADR-0001-architecture-qt6-vosk.md` (Accepted, Karl,
2026-08-03). Selected stack:

- **Language/UI:** C++ (hard constraint) on Qt 6, qtbase modules only. CMake requires >= 6.2 so the
  same tree configures on brew and on ubuntu apt; the development and showtime machines both run
  **6.11.1** (corrected 2026-08-05 by the context health check — this line said "6.8 LTS", which no
  machine in this project has ever used)
  (Core/Gui/Widgets), LGPLv3 dynamically linked.
- **Renderer:** from-scratch OOXML → in-memory slide model → QPainter/QTextLayout
  (text shaping, wrapping), QRawFont (embedded fonts), QImage (image codecs). Text+images
  tier only; unsupported elements → visible placeholders + warning list.
- **Speech:** Vosk **0.3.44** (Apache-2.0), on-device, **grammar-constrained** to the five
  phrases + number vocabulary (via `vosk_recognizer_new_grm` + a JSON phrase list). Model
  `vosk-model-small-en-us-0.15` (~40MB) bundled. **Deviation from ADR-0001's 0.3.45:** 0.3.45
  ships NO macOS build; 0.3.44 is the last with a macOS `universal2` (arm64) binary — required
  for the Apple-Silicon showtime machine. libvosk + model are **vendored via git-LFS** (fully
  self-contained offline build/run, Orchestrator decision); all pinned by SHA-256. See
  `third_party/PROVENANCE.md`.
- **Audio capture:** miniaudio (MIT, single-header) 0.11.25, vendored.
- **Parsing:** libzip 1.11 (OOXML zip), pugixml 1.15 (XML — DTD/entity processing off, see
  TM-010).
- **Logging:** spdlog 1.14 (MIT), structured, session-correlation IDs, day 1.
- **Build:** CMake ≥3.29 + Ninja; dependencies pinned via FetchContent; Qt in CI via
  aqtinstall; extra CI apt deps declared in `.github/ci-deps-apt.txt`.
- **Auth / secrets:** N/A (no accounts, no credentials — hard constraint).
- **Distribution:** GitHub Releases; macOS DMG via macdeployqt (primary); Windows/Linux
  packaging post-MVP (P2). Manual updates.

Rejected: SDL2 minimal stack (hand-rolled text layout + no a11y API — irreconcilable with
the WCAG-equivalent bar in one week); Qt + platform-native speech (loses Vosk's closed
grammar, adds a second backend); Tauri/Electron (violate the C++ constraint).

**Key architecture decision carried from the threat model (TM-018, availability is the top
asset for a live talk):** all slides are **pre-rendered to in-memory rasters on a background
thread at deck-load** — NOT lazily during the talk. This eliminates mid-talk UI-thread stalls
from a maliciously or accidentally complex slide. Memory-only (no on-disk render cache —
TM-011).

**AMENDMENT A3-1 (ratified by Karl Raulerson, 2026-08-04, F7 design review).** The original
wording of this section could not be implemented as written. Amended in three places; the
protective INTENT of each is preserved exactly.

1. **"QPixmap on a background thread" → QImage on the worker, QPixmap on the GUI thread.**
   Qt forbids constructing or using a QPixmap outside the GUI thread, and `SlideRenderer::render`
   already returns a QImage. The worker therefore produces **QImage**; the GUI thread performs the
   single `QPixmap::fromImage` on receipt. *Intent preserved:* all rasterization still happens
   off the UI thread at load time.
2. **"a hard per-slide deadline that degrades to a placeholder" → prevent / contain / isolate.**
   A deadline cannot abort a paint already in flight: `SlideRenderer::render` is one blocking
   pure call with no interruption points, and adding cooperative abort checks would make the
   already-security-audited pure renderer impure. The mandate is therefore met by three
   mechanisms instead of one: **prevent** — measure slide complexity BEFORE painting and emit a
   placeholder for any slide exceeding the caps (this is what actually stops the render bomb, and
   it is pure and unit-testable); **contain** — a render budget fed the measured elapsed time
   degrades all SUBSEQUENT slides and marks the deck degraded; **isolate** — rendering is
   off-thread, so even a pathological slide delays only its own readiness and can never block the
   UI. *Intent preserved:* a pathological slide cannot stall the app mid-talk.
3. **"sliding-window fallback IF memory pressure is detected" → the window is ALWAYS on, bounded
   by a 2 GB raster budget.** At 3840×2160 RGB32 a slide raster is ~33 MB, so a 300-slide deck
   would need ~10 GB and would swap the showtime machine — a worse availability failure than the
   threat being mitigated, and detecting pressure after the fact is too late. A contiguous window
   around the current slide is retained within a **2 GB** budget (Karl's ratified figure;
   minimum ±3 slides). *Intent preserved:* memory cannot be exhausted.

See `docs/design-notes/` for the F7 design and the review that surfaced these.

## 4. Threat Model & Risk/Mitigation Matrix

Full model: `docs/phase-1/threat-model.md` (23 threats TM-001…TM-023, Penetration Tester
persona). STRIDE coverage: S=3, T=4, R=2, I=4, D=7, E=3. No network attacker (offline).
Primary attack surface: the untrusted .pptx (zip + OOXML + embedded fonts + images).

Highest-severity live-presentation threats and their mitigations:
- **TM-018 (mid-deck render bomb, DoS):** legal-but-pathological slide (200k text runs,
  huge gradient path) stalls QPainter mid-talk. → Pre-render off-thread at load with
  per-slide complexity caps + hard deadline → placeholder (see §3).
- **TM-016 → TM-021 (malformed embedded font → in-process code exec via font engine) → 
  TM-022/023 (unsigned-bundle persistence):** → font-load sandboxing posture, size/format
  validation before QRawFont, and the deferred code-signing/notarization item is recorded
  as a residual risk for the unsigned MVP build (accepted for own-machine showtime).
- **Zip-bomb / zip-slip / oversized parts (TM-014/015/017, DoS/path-traversal):** →
  ≤1GB total decompressed + per-part caps + path canonicalization rejecting traversal
  (Manifesto Q9).
- **Audio command false-trigger during Q&A (TM-002/019):** → grammar-constrained recognizer
  + two-word phrases (Q1) + "pause presentation" discipline; push-to-talk is post-MVP.
- **Confidential-data leakage (TM-011/013):** → no deck content/audio to disk, no default
  core dumps (Q8), debug log is event-only, heard-text only in opt-in rehearsal log (Q7).
- **TM-010 (XML entity expansion):** structurally not exploitable (pugixml ignores DTDs);
  recorded as an INVARIANT to defend — a locked negative test + an ADR gate on any parser swap.

Every TM-ID carries a mitigation in the matrix at the end of the threat-model doc, keyed for
Phase 3.2 verification traceability.

### 4.1 Risk / mitigation matrix — all 23 threats, with Phase 3.2 verdicts

<!-- Last Updated: 2026-08-10 (Phase 3.2 security hardening) -->

Verified against the code and the suite on 2026-08-10. Full validation evidence, including
the attack payloads that were run, is in
`docs/test-results/2026-08-10_threat-model-validation.md`. Status vocabulary is deliberately
narrow, and **STRUCTURAL is stronger than MITIGATED** — it means the attack surface does not
exist rather than that a guard stops it:

| Status | Meaning |
|---|---|
| **VERIFIED** | A mitigation exists AND a test or measurement demonstrates it works |
| **STRUCTURAL** | The surface does not exist; a test pins the absence so it cannot return |
| **PARTIAL** | Mitigated in part; the residual is named |
| **ACCEPTED** | Not mitigated, consciously, with the reason recorded |
| **PHASE 4** | Deferred to release engineering; not reachable in a test build |

| TM | Threat | Mitigation | Status |
|---|---|---|---|
| TM-001 | Audience voice injection — anyone in the room is an operator | Two-word grammar; "pause presentation" gates voice; keyboard always independent; measured 0/6 natural sentences trigger | PARTIAL — 60/102 isolated near-miss fragments still match. Accepted by Karl 2026-08-09 for a 20-person room |
| TM-002 | Recorded/masked audio playback under acoustic cover | As TM-001, plus the Paused state; P key now reaches the same gate (BUG-73/81) | PARTIAL — same residual; verified on hardware 2026-08-06 and 2026-08-10 |
| TM-003 | Trojaned build spoofing the product | Ad-hoc signature only on test builds | PHASE 4 — Developer-ID signing and notarisation |
| TM-004 | Zip-slip / symlink escape through part names | **Nothing is ever extracted.** Parts are read by name from inside the archive (`zip_fopen`) | STRUCTURAL — `SEC/TM-004` runs a traversing Target and a `../../../../tmp/` part name and asserts no file appears on disk |
| TM-005 | Settings tampering to disarm the keyboard fallback | **There is no settings file.** Nothing is written to disk at run time (TM-011) | STRUCTURAL |
| TM-006 | Bundled Vosk model / grammar tampering | SHA-256 pinned in `third_party/PROVENANCE.md` and `sbom.json`; model extracted at BUILD time; grammar words round-tripped through the model vocabulary (BUG-65); dynamic graph required or voice refuses to arm | PARTIAL — an attacker with write access to the installed bundle is out of scope (TM-022/023, Phase 4) |
| TM-007 | OOXML part confusion — duplicate/ambiguous part names | Resolution is by name through libzip, first match | VERIFIED — `SEC/TM-007` asserts the answer is deterministic across runs |
| TM-008 | Unattributable slide changes after an incident | None. TM-011 forbids the log that would provide attribution | ACCEPTED — the confidentiality requirement and the attribution requirement are in direct conflict; confidentiality won (Manifesto Q7/Q8) |
| TM-009 | No record of which deck was rendered | Short content hash computed (`sha256Short`) but never persisted, for the same reason | ACCEPTED — same conflict as TM-008 |
| TM-010 | XML entity expansion / external entity file disclosure | pugixml does not resolve external entities and skips DTDs | STRUCTURAL — `SEC/TM-010` feeds a real `file:///etc/passwd` XXE and asserts no file content reaches the model; `SEC/TM-010/017` feeds a nine-level billion-laughs |
| TM-011 | Deck and room audio exfiltrated through crash artifacts | Nothing is written to disk at run time; no file is opened for writing anywhere in `src/`; the model is extracted at build time | VERIFIED — asserted by inspection each audit; heard text never leaves the decode → gate path |
| TM-012 | The transcript overlay discloses room audio to the room | There is no transcript overlay. Recognised text goes decoder → gate and nowhere else; Vosk's own stderr logging is silenced before any model loads | STRUCTURAL |
| TM-013 | Deck subject leaked by filenames on the holding screen / window titles | Error strings come from a closed vocabulary keyed by `LoadErrorKind`; `LoadError::message` (which carries the path) never reaches a widget | VERIFIED — `describeLoadError` is the only path to a dialog |
| TM-014 | Decompression bomb, incl. compression-method confusion | Caps enforced from the central directory BEFORE any part is read; sizes compared unsigned so a ZIP64 value cannot wrap negative | VERIFIED — hostile-input suite |
| TM-015 | Image pixel bomb via QImage | PNG/JPEG magic-byte allow-list before any decoder is constructed; declared-pixel cap (TM-018.3-A) rejects before painting | VERIFIED |
| TM-016 | Malformed embedded font → crash or code exec in the font engine | **Font data is never loaded from a deck.** Families are resolved against the system font database | STRUCTURAL — `SEC/TM-016` carries a malformed `.fntdata` part and asserts it is ignored |
| TM-017 | XML amplification and recursive-descent stack exhaustion | XML descent is ITERATIVE, not recursive; group nesting capped at 32; per-part size caps | VERIFIED — a 5.7 KB nested file previously killed the process and is now a fixture |
| TM-018 | The mid-deck render bomb | Four caps measured from the MODEL before painting; rasterising off-thread; raster window bounded at 2 GB | **FAIL (was VERIFIED until 2026-08-10, BUG-90)** — the image-pixel cap divides frame EMU by 9525 with INTEGER division, so a sub-pixel frame counts as ZERO, and the renderer decodes before it checks size. Reproduces the ~309 s stall the cap exists to prevent. Not reachable from Karl's own deck; live for any other |
| TM-019 | Audio pipeline wedge kills voice mid-talk | Every capture failure is recoverable by construction; the keyboard is independent of the speech engine and cannot be gated | VERIFIED — `AC:` group; and BUG-83 now contains an exception on the audio thread rather than terminating |
| TM-020 | Acoustic denial of the recogniser, and hostile "pause presentation" | Keyboard fallback; P key toggles the same gate so the presenter can always regain control without speaking | PARTIAL — a determined heckler can deny voice; they cannot deny the keyboard |
| TM-021 | Memory-safety exploitation in the parser → code execution | Iterative parse; ASan + UBSan + TSan run **locally only** (`build-asan/`, `build-tsan/`) — **not in CI**; four memory-safety defects found and fixed under sanitizers (BUG-52/56/57/79) | PARTIAL — no fuzzing campaign, and an independent reviewer found a fresh signed-overflow UB in this path in ~1 hour. **This row said "in CI" until 2026-08-10; that was false** |
| TM-022 | Dylib hijacking of the dynamic Qt frameworks | Bundle is self-contained and re-signed; `make-test-build.sh` FAILS if anything references Homebrew | PARTIAL — hardened runtime + library validation are PHASE 4 |
| TM-023 | Qt plugin-path hijacking (`qt.conf`, `QT_PLUGIN_PATH`) | Plugins are staged inside the bundle by `macdeployqt` | PHASE 4 — hardened runtime is what actually closes this |

## 5. Data Model

Full spec: `docs/phase-1/data-model.md`. Standalone app — no database. Two layers:

- **In-memory domain model (ephemeral, rebuilt per deck-open):** Presentation → Slide[] →
  ShapeElement (TextBox | Image | background); TextBox → Paragraph[] → TextRun
  (text + font/size/weight/color/style); Image (part ref, decoded pixels, rect, z-order);
  SlideBackground (solid | picture); EMU→pixel scaling; UnsupportedElementPlaceholder +
  LoadWarning records (drive F1's warning list). Command grammar as data: CommandType
  (5-value closed enum), NumberParseResult (Parsed | Unparseable). **Realized in F2/F3**
  (`matchCommand` + `RecognizerController`, Active/Paused dispatch gate); the recognizer
  boundary carries binding contracts — finalized-phrases-only, same-thread delivery,
  exception-guarded sink — recorded in the F2/F3 security audit and enforced by the
  future voice-engine adapter.
- **Persisted state (`settings.json`, schema-versioned):** `schema_version`,
  `overlay_fade_ms`, `mic_device_id` (null = system default; picker is P2), `keybindings`,
  `rehearsal_log_enabled` (default false), `recent_files` (paths only, max 10). Unknown keys
  rejected; older/newer `schema_version` → safe-load to defaults with a notice, never crash
  (this safe-load path IS the "rollback" for the only persisted schema).

**Ratified during synthesis (data-model gap 1):** `recent_files` is consolidated INTO
`settings.json` (one schema-versioned file) rather than a separate file — simpler forward-
migration story; recorded here as the Orchestrator-ratified resolution.

Sensitivity mapping (§5.1.1): deck content + embedded fonts + audio = Confidential
(memory-only, never persisted/logged); settings + recent-file paths = Internal; model files
= Public.

## 6. Data Migration Plan

N/A — no legacy system, no existing data to import. The only persisted artifact is
`settings.json`, whose forward-migration + safe-load fallback is specified in §5 / the data
model doc. Recorded N/A deliberately (not forgotten).

## 7. Auth & Identity Strategy

N/A — single-user local application, no accounts, no authentication, no authorization
surface (hard constraint, intake §6.4). The only access-control concern is Confidential-data
containment, covered in §4/§5. Recorded N/A deliberately.

## 8. Observability & Logging Strategy

Structured logging via spdlog from day 1. Every significant operation emits a record with
timestamp, severity, and a session-scoped correlation ID (one per app run). **Content
constraints (binding, from Manifesto Q7/Q8):** logs record command events, match confidence,
and timings only; NO heard text by default (heard text only in the opt-in, local,
user-deletable rehearsal log, default off); NEVER deck content or audio buffers; no default
core dumps. Log file lives in the OS-standard location (macOS: `~/Library/Logs/powerpoint-voice/`).
Desktop app = no server observability; crash visibility for MVP is the local log + a
graceful in-app error surface (Sentry-style crash reporting is post-MVP and would require an
opt-in + network, so it is explicitly deferred to preserve the offline guarantee).

## 9. UI Component Specifications

Full spec: `docs/phase-1/ui-scaffolding.md`. Four views (StartView, LoadReportView,
PresentationView, HoldingView) + two core components with all four states each:
- **SlideCanvas** — displays the pre-rendered slide QPixmap; states Empty / Loading
  ("Rendering slides…", UI never blocks) / Error (placeholder + note, nav still works) /
  Success.
- **TranscriptOverlay** — heard text + matched command + persistent listening-state glyph;
  states Empty (glyph only) / Loading (dimmed partial) / Error (engine-dead / mic-unavailable
  persistent banners, never color alone) / Success (transient command echo; persistent PAUSED).

Minimal dark theme only. Keyboard surface (strict parity, Q2): →/Space next, ← previous,
P pause-toggle, digits+Enter go-to-slide, Esc holding/exit.

## 10. Coding Standards

- **Language:** C++20 (Qt 6.8 supports it; `-std=c++20`). No exceptions across the
  OOXML-parse boundary escape unhandled — untrusted input parsing wraps failures into typed
  results (LoadWarning / hard-reject), never a crash on the presentation thread.
- **Style:** enforced by `.clang-format` (LLVM base, 4-space, 100-col) and `.clang-tidy`
  (bugprone-*, cppcoreguidelines-*, performance-*) — both already wired into the CI template;
  warnings-as-errors in CI.
- **Naming:** `PascalCase` types, `camelCase` methods/vars, `SCREAMING_SNAKE` constants,
  `m_` member prefix (Qt-idiomatic).
- **Never-do:** no raw `new`/`delete` (RAII / smart pointers / Qt parent-ownership); no
  blocking work on the UI thread (all parsing + pre-render off-thread — TM-018); no writing
  deck content or audio to disk (§4/§8); no dynamic loading of code from the .pptx; no
  network calls of any kind (offline invariant — a CI grep guard is a candidate).
- **Dependencies:** exact-version pinned via CMake FetchContent + committed; a new dependency
  needs justification (CLAUDE.md rule).

## 11. Build & Distribution Strategy

- **Build:** CMake ≥3.29 + Ninja, `-DCMAKE_BUILD_TYPE=Release`, `compile_commands.json`
  exported for clang-tidy. Qt provisioned in CI via aqtinstall; system deps for the ubuntu
  CI runner declared in `.github/ci-deps-apt.txt` (the cpp CI template reads it).
- **CI:** the authored `cpp.yml` (WALK ISSUE-002) — build/test/format/lint guarded on
  scaffold presence, gitleaks, dependency audit (osv-scanner when a manifest exists),
  strong-copyleft license check, governance + Semgrep jobs.
- **Packaging:** macOS DMG via `macdeployqt` (bundles Qt frameworks + Vosk lib + model).
  Windows (`windeployqt` → NSIS/portable) and Linux (`linuxdeploy` → AppImage) are P2 —
  code stays portable and CI-built throughout, validation post-show.
- **Code signing:** deferred post-MVP (Manifesto/intake §10) — unsigned macOS build accepted
  for own-machine showtime (Gatekeeper right-click-open). Apple Developer Program in the
  deferred tooling list. Residual risk recorded (TM-022/023).
- **Release pipeline:** `release.yml` needs C++ build/sign steps authored in Phase 4
  (WALK ISSUE-003 — the generated template has language-placeholder TODOs for cpp).

## 12. Test Strategy

- **Unit (ctest + a lightweight C++ framework — doctest, MIT, header-only, pinned):** the
  from-scratch renderer's OOXML→model mapping, EMU scaling, number-word normalization
  ("fifteen"/"one five"/"15" → int with range checks), grammar matching, settings
  schema load/migration, zip-bomb/zip-slip guards. Tests-first per the Build Loop.
- **Integration:** load synthetic fixture decks (text+images tier) → assert slide count,
  element extraction, placeholder generation for a deliberately-unsupported element;
  malformed/oversized/zip-bomb fixtures → assert graceful reject (TM-014/015/017 regression
  tests bound to threat IDs).
- **Security:** Semgrep (CI + pre-commit), gitleaks, the license gate; the offline invariant
  (no network symbols reachable) as a build-time/audit check; each mitigated TM-ID gets a
  test or a documented manual verification in Phase 3.2.
- **Accessibility (mandated — Competency "Partially"):** Qt Accessibility tree inspected via
  macOS Accessibility Inspector + VoiceOver pass on the primary journey; contrast + keyboard
  operability checks. Phase 3.4.
- **Performance:** slide-change <200ms, overlay <500ms, command-to-action p95 ≤1.5s
  (Manifesto §2.3) measured on the showtime MacBook; pre-render load-time + memory budget
  measured against a 300-slide fixture.
- **UAT:** every 2 features (intake §11.5), 1 tester (Karl), GitHub Issues tracker, agent
  testers (automated/exploratory/cross-platform) in parallel. SEV SLAs: SEV-1 24h / SEV-2 7d.
- **Pass/fail & Phase 3 entry:** no open SEV-1/2, CI green, all MVP features built + tested,
  every scanner PASS or attested. Results archived in `docs/test-results/`.

## 13. Orchestrator Profile Summary

Karl Raulerson — Product/UX, security, DevOps, performance, local-storage: "Yes" (self is
the quality gate). Frontend/UI (Qt), Accessibility, Platform-specific (macOS): "Partially" →
automated tooling MANDATORY in Phase 3 (Qt a11y audit, UI test automation, macOS platform
test pass). Backend/API and Mobile: N/A (no server, no mobile). No prior Qt/C++-GUI
experience claimed → the renderer and Qt code get heavier automated coverage (≥ the desktop
module's >80% guidance for "No/Partially" languages) and closer Build-Loop review.

## 14. Accessibility Requirements

From intake §9: WCAG AA-equivalent for a native desktop app — full keyboard operability
(also Must-Have F6), text/accessible labels on every interactive element, contrast ≥4.5:1,
visible focus states, never color-alone for meaning (engine-dead/mic states pair icon+text).
Verified by the mandated Phase 3.4 accessibility audit (VoiceOver / Accessibility Inspector).
Minimal-dark is the only theme; dark-mode contrast must still meet the ratio.

## 15. Platform-Specific Requirements

From the Desktop Platform Module + ADR: macOS 14+ primary (showtime), Windows 10+ / Ubuntu
22.04+ secondary. macOS specifics: TCC microphone permission prompt handled gracefully
(deny → mic-unavailable banner + keyboard fallback); Apple Silicon + Intel (universal or
arm64-first for the showtime Mac); Retina/HiDPI via Qt; menu-bar conventions; unsigned-build
Gatekeeper path documented. Multi-display hotplug (QScreen) — disconnect mid-show falls back
to the laptop screen preserving position (F7). Sleep/screensaver suppression during
presentation mode. No elevated privileges. File handling: path-traversal-safe (TM-015).

## 16. Context Management Plan

Small project (<30 files expected). Full Bible + relevant artifact per session. The Bible +
the specific `docs/phase-1/*` artifact + last 2-3 source files is sufficient resume context.
Qdrant MCP available for cross-session memory (architecture decisions, debugging
breakthroughs, phase transitions per CLAUDE.md). WALK-STATE.md is the walk-level resume file.
