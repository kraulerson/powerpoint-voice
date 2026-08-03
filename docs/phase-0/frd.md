> **POST-REVIEW AMENDMENT (2026-08-03, binding):** At Phase 0 review Karl resolved all flagged questions and CHANGED THE COMMAND GRAMMAR to two-word phrases: "next slide", "previous slide", "pause presentation", "continue presentation" (plus "go to slide N" unchanged). Bare single-word commands in this document are superseded. The B key / blank-screen is DROPPED from MVP (strict five-command parity). Authoritative resolutions: PRODUCT_MANIFESTO.md §8 (Q1-Q13).
# Functional Requirements Document — powerpoint-voice

**Date:** 2026-08-03
**Status:** Draft

> Phase 0 Step 0.1 output ("With Intake — Validation" path). Source of truth: `PROJECT_INTAKE.md` §2 (business context), §4 (features), §6.4 (hard constraints). This document expands the Intake's business-logic triggers and failure states into complete specifications; it adds **no** features beyond the Intake. Items requiring Orchestrator resolution are tagged **C1–C5** (contradictions), **D1–D12** (implicit dependencies), and **W1–W4** (Will-Not-Have cross-checks). No new ID prefix is minted (per `docs/IDENTIFIERS.md`); requirement numbering below is section-scoped to this document.
>
> Hard constraints honored throughout: C++ implementation; **fully offline** (zero runtime network dependency, including speech recognition); no authentication/accounts; **fixed command grammar only** — "next", "previous", "pause", "continue", "go to slide N"; renderer scope is the **text+images tier only**. Context: live executive presentation ~2026-08-10; every specification is written for single-show reliability with keyboard fallback as the degradation backbone.

---

## Must-Have (MVP)

| # | Feature | Logic Trigger | Failure State | Rationale |
|---|---------|--------------|---------------|-----------|
| 1 | PPTX load & render (text+images tier, from-scratch C++ renderer) | If the user opens a .pptx (File→Open, drag-drop, CLI arg), then validate → parse OOXML → render every slide (text boxes with formatting, placed images, solid/picture backgrounds) → pixel-stable full-screen slide view; slide change display <200 ms. Full spec: §1 below. | Invalid/corrupt → error naming the failing part, app keeps running. Unsupported element → visible placeholder + consolidated load-time warning (never a silent wrong render). >200 MB or >300 slides → reject stating the limit. Full flows: §1-E. | Nothing else matters if the deck isn't on screen; renderer risk is fenced to this tier (Intake §11 risk 1). |
| 2 | Voice navigation: "next" / "previous" | If the always-on recognizer matches "next"/"previous" while presenting (grammar-constrained, confidence ≥ threshold), then move exactly one slide, visible ≤1.5 s p95 from end of utterance, and echo the command in the overlay. Full spec: §2 below. | At last/first slide → no-op + overlay notice. Low confidence → no action; overlay shows heard text. Mic unavailable → persistent banner + auto-retry; keyboard unaffected. Full flows: §2-E. | The core hands-free promise (Intake §2.1). |
| 3 | Recognition control: "pause" / "continue" | If "pause" is matched, then suspend matching to a continue-only grammar and show a persistent PAUSED indicator; "continue" restores the full grammar. Idempotent. Full spec: §3 below. | Engine crash → watchdog auto-restart + overlay alert; repeated crash → voice offline banner, keyboard-only. Keyboard pause toggle works regardless of engine state. Full flows: §3-E. | Prevents mid-talk speech from triggering slides — a live-show necessity. |
| 4 | "Go to slide N" with robust numbers | If "go to slide <number>" is matched with N in 1..M (deck size), then jump directly to slide N; accepted forms: digits ("15"), number words ("fifteen"), digit-by-digit ("one five") — all enumerated inside the fixed grammar. Full spec: §4 below. | Out of range → overlay "deck has M slides", no movement. Unparseable → overlay shows heard text, no movement. Full flows: §4-E. | Direct jump during Q&A is the differentiator over any clicker (Intake §2.2). |
| 5 | Live command-transcript overlay | If any utterance is processed, then display heard text + matched command (or "no match") within 500 ms in the reserved strip; transient entries auto-fade ~3 s; persistent status class does not fade. Full spec: §5 below. | Overlay never exceeds its reserved strip; overlay rendering failure is contained and must never take down the slide view or input. Full flows: §5-E. | Instant recognition diagnosis at showtime and the §2.3 measurement instrument. |
| 6 | Keyboard fallback for every command | If arrow keys / space / B / P / typed-number+Enter are pressed at any time, then execute the identical action to the corresponding voice command via a dispatcher that bypasses the speech engine entirely. Full spec: §6 below. | Fully independent of the speech engine — works when the engine is dead, paused, or never started; remains functional in every failure state of Features 1–5 and 7. Full flows: §6-E. | The graceful-degradation backbone; §2.3 counts keyboard use as degradation, not failure. |
| 7 | Presentation mode UI (minimal dark, dual-display aware) | If a presentation is started, then render the full-screen minimal-dark view (slide, overlay strip, slide counter) and route the slide view to the external display when attached. Full spec: §7 below. | No external display → single-screen full-screen. Display disconnect mid-show → fall back to the laptop screen without crashing or losing position. Full flows: §7-E. | The projector is the product surface on 2026-08-10. |

### Detailed Specifications

#### §1 Feature 1 — PPTX load & render (text+images tier)

**Expanded logic trigger**

- 1.1 **Open paths (three, converging on one pipeline):** File→Open dialog; drag-and-drop onto the app window; CLI argument (`powerpoint-voice <path>.pptx`). Identical validation and rendering regardless of path.
- 1.2 **Validation order (first failure aborts with its specific reason):** file exists and is readable → valid ZIP archive → contains `ppt/presentation.xml` → file size ≤200 MB → slide count ≤300.
- 1.3 **Render scope — the text+images tier, exhaustively:** text boxes/placeholders with run formatting (font family, size, bold/italic/underline, color, horizontal alignment, bullet/numbered lists, line spacing); placed raster images (position, size, z-order; formats per D12); solid-color slide backgrounds; picture backgrounds. **Everything not in this list is an unsupported element** (charts, SmartArt, tables, shapes, WordArt, gradients/patterns, EMF/WMF/SVG images, OLE objects, embedded audio/video). Renderer scope is fixed at this tier — no silent partial support of higher tiers.
- 1.4 **Output:** pixel-stable slide surfaces (same input → same pixels across runs); slide-change display <200 ms (Intake §5.2); slides retain their original 1..M numbering under all conditions (Feature 4 depends on stable numbering).
- 1.5 **Fonts (Intake §11 risk 3):** use fonts embedded in the package when present; otherwise match installed system fonts; any substitution is reported per 1-E6 — **never silent**.
- 1.6 **Reload semantics:** opening a new deck replaces the current one only after the new deck parses successfully; a failed open never destroys the currently loaded deck or presentation position.

**Error & recovery flows**

- 1-E1 **Invalid/corrupt file:** error dialog naming the failing part (e.g., "not a ZIP archive", "`ppt/presentation.xml` missing", "`slide12.xml` is malformed XML"); the app returns to its prior state (previous deck or open screen) and keeps running. Never exits, never renders a guess.
- 1-E2 **Over-limit:** reject before parsing slides, stating the measured value and the limit ("Deck is 240 MB; the limit is 200 MB" / "Deck has 312 slides; the limit is 300"). No partial load.
- 1-E3 **Single-slide parse failure in an otherwise valid deck:** that slide renders as a full-slide placeholder naming the slide number and reason; it remains in the deck at its position so numbering does not shift; it is listed in the load warning (1-E4).
- 1-E4 **Unsupported element:** visible placeholder at the element's bounds naming the element type, plus one consolidated load-time warning listing **every** unsupported item with its slide number. The presenter must be able to review this list before presenting — no silent wrong render, ever.
- 1-E5 **Image decode failure:** placeholder at the image bounds + entry in the 1-E4 warning list.
- 1-E6 **Font substitution:** entry in the 1-E4 warning list naming slide, original font, and substitute.

**Rationale:** Must-Have — the deck on screen is the precondition for every other feature; the placeholder-plus-warning policy is the agreed mitigation for the from-scratch renderer schedule risk (Intake §11 risk 1).

#### §2 Feature 2 — Voice navigation: "next" / "previous"

**Expanded logic trigger**

- 2.1 The recognizer runs continuously while a presentation is active, matching **only** the fixed grammar (hard constraint). Audio is processed in memory and never persisted (W3).
- 2.2 A match of "next"/"previous" at or above the confidence threshold advances/rewinds **exactly one** slide; the slide change is visible ≤1.5 s p95 from end of utterance (see C4 for p95 vs. absolute reconciliation). The 1.5 s budget decomposes as: recognition ≤1.3 s + render/display <200 ms (Intake §5.2).
- 2.3 Every executed command is echoed in the overlay (Feature 5) — command name, e.g. `next ✓`.
- 2.4 Rapid sequential commands execute in order, one slide per utterance — no coalescing, no double-advance from a single utterance.

**Error & recovery flows**

- 2-E1 **At last slide, "next"** (or first slide, "previous"): no movement; transient overlay notice "End of deck — slide M of M" (resp. "At first slide").
- 2-E2 **Low-confidence match:** no action; overlay shows the heard text and "no match" — the presenter can immediately re-speak or use the keyboard.
- 2-E3 **Microphone unavailable** (missing device, OS permission denied/revoked, device seized by another process, unplugged mid-show): persistent status banner "Microphone unavailable — keyboard control active"; the app re-attempts capture every 5 s and clears the banner on success. The keyboard path (Feature 6) is unaffected throughout.

**Rationale:** Must-Have — this is the hands-free core of Intake §2.1; without it the product is a viewer.

#### §3 Feature 3 — Recognition control: "pause" / "continue"

**Expanded logic trigger**

- 3.1 On "pause": command matching is restricted to a continue-only grammar; a **persistent** PAUSED indicator is shown for the entire paused period (persistent status class, §5 — see C1). No other voice command executes while paused, including "go to slide N".
- 3.2 On "continue": the full grammar is restored; the overlay confirms ("Listening").
- 3.3 **Idempotency:** "pause" while paused and "continue" while active change nothing; the overlay acknowledges the current state.
- 3.4 The keyboard pause toggle (P, Feature 6) drives the same state machine and works regardless of engine state.
- 3.5 Utterance handling while paused: see **C3** (contradiction with Feature 5's "any utterance is displayed") — proposed resolution: transcribe and display, execute nothing except "continue".

**Error & recovery flows**

- 3-E1 **Engine crash or hang:** a watchdog (D4) detects it and restarts the engine automatically; overlay alert "Speech engine restarted"; the pause/active state held before the crash is restored.
- 3-E2 **Repeated crash (≥3 restarts in 60 s):** stop auto-restarting; persistent banner "Voice control offline — keyboard control active"; presentation continues keyboard-only. A manual re-enable action is available outside presentation flow.
- 3-E3 The keyboard pause toggle and all Feature 6 keys work in every engine state: running, paused, restarting, offline.

**Rationale:** Must-Have — a live talk contains continuous speech; without a reliable mute, accidental matches would move slides mid-sentence.

#### §4 Feature 4 — "Go to slide N" with robust numbers

**Expanded logic trigger**

- 4.1 **Grammar:** "go to slide" followed by a number in three accepted forms — digits ("15"), number words ("fifteen", "one hundred twenty"), digit-by-digit ("one five"). The number vocabulary is an **enumerated part of the fixed grammar**, bounded to 1..300 and constrained at load to 1..M for the open deck — this is not free dictation (W2).
- 4.2 Valid N in 1..M → jump directly to slide N; overlay echo ("go to slide 15 ✓"); same ≤1.5 s p95 budget as §2.2.
- 4.3 N equal to the current slide → no movement; overlay acknowledgment.
- 4.4 Not matched while PAUSED (§3.1).

**Error & recovery flows**

- 4-E1 **N out of range** (0, or > M): no movement; overlay "Deck has M slides".
- 4-E2 **Unparseable number:** no movement; overlay shows the heard text and "no match".
- 4-E3 Ambiguous multi-form utterances resolve to exactly one reading or are treated as unparseable (4-E2) — the system never guesses between two candidate slide numbers.

**Rationale:** Must-Have — direct jump during Q&A is the capability no clicker has and a stated reason this app exists (Intake §2.1, §2.2).

#### §5 Feature 5 — Live command-transcript overlay

**Expanded logic trigger**

- 5.1 Every processed utterance produces an overlay entry — heard text plus matched command or "no match" — within 500 ms of the recognition result.
- 5.2 **Transient class** (transcript echoes, no-op notices): auto-fade after ~3 s (fade duration is a settings key, D6).
- 5.3 **Persistent status class** (PAUSED, microphone unavailable, voice offline, engine restarted): displayed until the condition clears — does **not** auto-fade. This two-class model is the proposed resolution of **C1** and requires Orchestrator approval.
- 5.4 **Reserved strip:** the overlay occupies a single strip along the bottom edge of the slide view, height ≤10% of screen height (**specific value proposed — flagged for Orchestrator review**; the Intake says only "the reserved strip"). Overlay content never draws outside the strip.
- 5.5 **Accessibility (Intake §9):** text contrast ≥4.5:1 against the strip background; state is conveyed by text/icon, never color alone.
- 5.6 Keyboard-initiated actions echo identically to voice-initiated ones (Feature 6).

**Error & recovery flows**

- 5-E1 **Overlay rendering failure:** the failure is contained (isolated draw path); the overlay is disabled, while the slide view, voice commands, and keyboard input all continue. The condition is logged (D11) and signaled by a marker in the slide-counter region (**mechanism flagged** — the overlay itself cannot announce its own death).
- 5-E2 The overlay never obscures more than the reserved strip under any content length — long heard-text is truncated with an ellipsis rather than growing the strip.

**Rationale:** Must-Have — it is both the presenter's instant diagnosis tool when recognition degrades and the measurement instrument for the §2.3 rehearsal criteria (with D11).

#### §6 Feature 6 — Keyboard fallback for every command

**Expanded logic trigger**

- 6.1 **Mapping (identical action to the corresponding voice command):**

  | Key(s) | Action | Voice equivalent |
  |---|---|---|
  | Right / Down / Space / PageDown | next slide | "next" |
  | Left / Up / PageUp | previous slide | "previous" |
  | P | pause/continue toggle | "pause" / "continue" |
  | typed digits + Enter | go to slide N (digits echoed while typing; Esc cancels entry) | "go to slide N" |
  | B | **unresolved — see C2** (no corresponding voice command exists in the fixed grammar) | none |
  | Esc | exit presentation mode (lifecycle, D7) | none — lifecycle action, not a slide command |

- 6.2 **Engine independence, end-to-end:** key events feed the same command dispatcher as voice but bypass the recognizer entirely. The keyboard path must work when the engine is dead, restarting, paused, offline, or never initialized.
- 6.3 Keyboard actions are echoed in the overlay exactly as voice commands are (§5.6); the same range/validation rules apply (typed 999 in a 47-slide deck → 4-E1 message).

**Error & recovery flows**

- 6-E1 The keyboard path must remain fully functional in **every** failure state defined in §§1-E–5-E and §7-E. Any defect that disables keyboard control while presenting is SEV-1 by definition — it removes the show's last line of defense.
- 6-E2 Invalid typed slide number → same overlay feedback as the voice path; entry buffer clears.

**Rationale:** Must-Have — §2.3 defines keyboard use at showtime as graceful degradation, not failure; this path is what makes betting a live executive presentation on a new app defensible.

#### §7 Feature 7 — Presentation mode UI (minimal dark, dual-display aware)

**Expanded logic trigger**

- 7.1 Starting a presentation from a loaded deck enters the full-screen minimal-dark view: current slide, overlay strip (§5.4), and slide counter ("12 / 47"). Dark is the only theme in MVP (Intake §9).
- 7.2 **External display attached:** the slide view is routed to the external display; the laptop screen shows the identical slide view with overlay and counter (an operator mirror — presenter-notes view is deferred to v1.1; **this mirror choice is specific and flagged for Orchestrator review**, as the Intake does not say what the laptop shows).
- 7.3 **External display attached mid-show:** the slide view moves to it without losing the current slide position.
- 7.4 **Aspect mismatch** between deck slide size and display: letterbox/pillarbox on black — never stretch or crop (**specific and flagged**).
- 7.5 While presenting, system sleep and screensaver are suppressed (D8); suppression is released on exit.
- 7.6 No external display → single-screen full-screen mode on the laptop display.

**Error & recovery flows**

- 7-E1 **Display disconnect mid-show:** fall back to laptop full-screen at the same slide, without crashing; transient overlay notice. Reconnect → 7.3 applies.
- 7-E2 Display topology events (connect/disconnect/resolution change) must never crash the app or lose slide position.
- 7-E3 Full-screen acquisition failure → windowed fallback at maximum size with a visible warning; all commands still function.

**Rationale:** Must-Have — the projector is the product surface on 2026-08-10; projector hotplug is a routine conference-room event, not an edge case.

### Contradictions Identified (Orchestrator resolution required)

- **C1 — Overlay auto-fade vs. persistent banners (F5 vs. F2/F3).** F5 specifies entries "auto-fading after ~3 s", but F2 requires a "persistent banner" (mic unavailable) and F3 a PAUSED indicator that must remain visible while paused. A single auto-fading strip cannot satisfy both. **Proposed resolution (spec'd in §5.2–5.3):** two message classes — transient (fades) and persistent status (until cleared). Approve or amend.
- **C2 — Keyboard "B" has no corresponding voice command.** F6's trigger lists key "B" and promises "the identical action to the corresponding voice command", but the fixed grammar (hard constraint) contains no matching command. By presenter convention B = black-screen toggle — which would be an **action that appears nowhere in §4.1's feature list or the grammar**. Options: (a) B = keyboard-only black-screen toggle, documented as such; (b) drop B from the mapping. Blackout has **not** been added to this FRD (scope rule); §6.1 marks B unresolved pending your ruling.
- **C3 — Paused transcription (F3 vs. F5).** F3 says paused means "listening only for 'continue'"; F5 says "if **any** utterance is processed" it is displayed within 500 ms. Are non-"continue" utterances transcribed to the overlay while paused? **Proposed resolution (§3.5):** yes — transcribe and display, execute nothing but "continue" (preserves the diagnosis value and §2.3 measurement); alternatively, suppress display entirely while paused. Decide.
- **C4 — Latency: absolute vs. p95.** F2's trigger reads as an absolute "within 1.5 s", while §2.3's success criterion is "≤1.5 s **p95**". This FRD adopts p95 (measured per D11) to match the success criteria; no absolute worst-case ceiling is defined anywhere in the Intake. Confirm p95, or set a hard ceiling.
- **C5 — "No transcription beyond the live overlay" vs. the debug session log.** §2.4-5 excludes "transcription beyond the live command overlay", yet §5.2 defines an optional local debug session log. Reconciled reading adopted here: the debug file (off by default) stores command/decision **text** only — heard text, matched command, timestamps — never audio in any form (see W3). Confirm this reading.

### Implicit Dependencies (not listed in the Intake)

- **D1 — Speech model bundling.** The fully-offline hard constraint plus Will-Not-Have 2 means the on-device engine's acoustic/grammar model files **must ship inside the app bundle/DMG** — no first-run or on-demand download is permissible (see W1). Consequences to plan for: DMG size includes the model; the model's license must permit redistribution. (Engine selection itself is a Phase 1 decision — this dependency is stated technology-neutrally.)
- **D2 — macOS microphone permission (TCC).** Even a fully offline app must pass the OS mic-permission prompt: first-run prompt, usage-description string in the app metadata, and denial/revocation feeding the 2-E3 mic-unavailable flow.
- **D3 — Audio capture pipeline.** Default input-device selection (device picker is deferred to v1.1), sample-rate conversion to the engine's rate (~16 kHz per §5.1), in-memory buffers only (W3).
- **D4 — Speech-engine watchdog.** F3's "automatic engine restart" failure state implies a supervisor that detects crash/hang and restarts the engine (§3-E1/E2). Not listed as a feature anywhere in the Intake.
- **D5 — Font handling subsystem.** Embedded-font extraction from the .pptx, system-font matching, and visible substitution reporting (1.5, 1-E6) — required by Known Risk 3 ("design defensively; substitution must be visible, never silent").
- **D6 — Settings persistence.** §5.1/§5.4 require a schema-validated JSON settings file (overlay fade, mic device, keybindings; unknown keys rejected) and a recent-files list (paths only): storage location in the platform config directory, corrupt-file recovery (fall back to defaults with a warning, never crash at launch), and settings lifetime per §5.4.
- **D7 — App lifecycle.** Pre-presentation state (open/landing screen when no deck is loaded), enter/exit presentation (Esc), confirm-on-quit while presenting, single-deck-at-a-time model, and behavior when a CLI-passed path is invalid (1-E1 into the open screen).
- **D8 — Sleep/screensaver suppression.** While presenting, the system must be prevented from sleeping or starting the screensaver (a mid-show blanked projector is a show-stopper); released on exit (§7.5).
- **D9 — Display management subsystem.** Display enumeration, hotplug event handling, per-display full-screen, and aspect handling (§7.2–7.4, 7-E1/E2).
- **D10 — OOXML parsing dependencies.** ZIP inflation and XML parsing require bundled, pinned C++ libraries (per CLAUDE.md pinning rule); the .pptx is untrusted input — parser hardening is in renderer scope.
- **D11 — Timestamped structured session log.** §2.3's recognition-rate and latency criteria are measured from "timestamped session log" and the overlay tally — an in-memory session log with an optional off-by-default local debug file (bounded by C5/W3) is therefore a functional dependency of the success criteria, not just an ops nicety.
- **D12 — Image codecs.** F1's "placed images" requires decoders for the raster formats decks actually contain (PNG, JPEG, GIF, BMP) as bundled/pinned libraries; formats outside the supported set follow the 1-E4/1-E5 placeholder path (e.g., EMF/WMF/SVG/TIFF).

### Recommendations (not in scope unless Orchestrator approves)

- Resolve **C2** as option (a): B = keyboard-only black-screen toggle. It matches universal presenter muscle memory and costs little; but it is a new action, so it is not specified above.
- A pre-show "mic check" screen (speak the five commands, see them recognized) would de-risk the 2026-08-10 room; deliberately not added — Practice Mode is already deferred to v1.1.
- Expose the recognition confidence threshold as a settings key (D6) to allow tuning at the 2026-08-08 rehearsal without a rebuild; not added pending approval.

---

## Should-Have (v1.1)

| # | Feature | Description | Deferred Because |
|---|---------|-------------|-----------------|
| 1 | Timer / elapsed clock in presenter overlay (P1) | Elapsed-time display in the operator view's overlay region | Not needed to control slides on 2026-08-10; adds overlay complexity during the reliability-critical window |
| 2 | Practice mode with per-command accuracy stats (P1) | Scripted run mode that tallies recognition success per command | §2.3's 50-command rehearsal can be tallied manually from the overlay/log (D11); automation is post-show value |
| 3 | Microphone input-device picker (P1) | Settings UI to select among available input devices | MVP uses the system default input (D3); the showtime machine's configuration is known and fixed |
| 4 | Presenter-notes view on laptop with clean projector output (P2) | Laptop shows notes + next slide while projector shows the slide only | MVP mirrors the slide view (§7.2); notes rendering expands the renderer surface right where the schedule risk lives |
| 5 | Windows/Linux packaging and full validation (P2) | Installers and validated support for Windows 10+ / Ubuntu 22.04+ | Showtime machine is macOS; code stays portable and CI-built throughout (Intake §1), validation lands post-show |

---

## Will-Not-Have

| # | Feature | Exclusion Rationale |
|---|---------|-------------------|
| 1 | Deck editing, creation, or export of any kind | Not a slide editor (§2.4-1); the app renders and navigates an existing .pptx, nothing more |
| 2 | Any network-dependent capability: cloud speech, accounts, telemetry, update phone-home | Fully-offline is a hard constraint (§6.4) — conference-room Wi-Fi cannot be trusted; no accounts is also a hard constraint |
| 3 | Free-form dictation, wake words, or any speech feature beyond the fixed command grammar | Fixed grammar is a hard constraint; grammar constraint is also the recognition-accuracy mitigation (§11 risk 7) |
| 4 | Animation/transition playback, embedded video/audio rendering | Static rendering only (§2.4-4); renderer scope is fenced to the text+images tier (§11 risk 1) |
| 5 | Recording or persisting any audio | Confidential live room audio (§5.1) is processed in memory only — never stored, never transmitted |

### Will-Not-Have Cross-Check (Must-Have conflicts)

- **W1 — Exclusion 2 vs. Features 2/3/4 (on-device speech).** Offline speech recognition still requires model files at runtime. A "download the model on first run" design — common for on-device engines — would violate Exclusion 2. **Resolution required and adopted here: models ship inside the app bundle (D1).** With bundling mandated, no conflict remains.
- **W2 — Exclusion 3 vs. Feature 4 (robust numbers).** "Robust number parsing" must not be implemented via free dictation of a number expression. Compliant form (spec'd in §4.1): the number vocabulary is enumerated inside the fixed grammar, bounded 1..300 and constrained to 1..M at deck load.
- **W3 — Exclusion 5 vs. Feature 5 + §5.2 debug log.** The overlay and the optional debug log handle recognized **text** only; raw audio exists solely in transient in-memory capture buffers discarded after recognition. No code path may write audio to disk in any format (ties to C5).
- **W4 — Exclusion 4 vs. Feature 1 (embedded media in real decks).** Slides containing video/audio/animated content are still content-bearing; per 1-E4 those elements get visible placeholders and load-warning entries — exclusion from *playback* never becomes silent omission from *rendering*.

**Conclusion:** no Must-Have requires an excluded capability, **provided** W1's model-bundling requirement (D1) is adopted as binding.

---

## Review Checklist

- [x] Every Must-Have feature has a logic trigger (If/Then/Output) — expanded in §§1–7
- [x] Every Must-Have feature has a defined failure state — error/recovery flows §§1-E–7-E
- [x] Every feature is categorized (Must/Should/Will-Not)
- [x] At least 3 Will-Not-Have items are listed (5 listed)
- [ ] No feature is ambiguous enough to be interpreted two ways — **open until the Orchestrator resolves C1–C5 and the flagged specifics (§5.4 strip height, §7.2 laptop mirror, §7.4 letterboxing)**
