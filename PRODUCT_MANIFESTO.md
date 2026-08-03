# Product Manifesto — powerpoint-voice

<!--
  This document is the foundational artifact produced during Phase 0.
  It defines what the product does, who it serves, and what is in/out of scope.
  It is the north star for all subsequent phases.

  Completion gates entry to Phase 1. All 8 numbered sections must be filled out.
  Appendices are track-conditional — see inline notes.

  Do not alter headings or remove sections. Add content within the placeholders.
-->

**Status:** Approved
**Approved By:** Karl Raulerson (Project Sponsor)
**Approval Date:** 2026-08-03
**Phase Gate:** Phase 0 → Phase 1

---

## 1. Product Intent

<!-- Source: Phase 0 Step 0.4. See builders-guide.md for the full prompt and review checklist. -->

powerpoint-voice is a fully-offline C++ desktop application that lets a presenter control a PowerPoint deck by voice — "next slide", "previous slide", "pause presentation", "continue presentation", and "go to slide N" — with a keyboard equivalent for every command, a live command-transcript overlay, and a minimal dark UI that routes slides to the projector. It exists because Karl Raulerson presents to executives on ~2026-08-10 and needs to walk the room hands-free, jump directly to any slide during Q&A, and trust the system in a conference room whose network cannot be trusted: recognition runs entirely on-device, and every voice pathway degrades gracefully to the keyboard. The deck is Confidential; nothing about it ever leaves the machine.

---

## 2. Functional Requirements

<!-- Source: Phase 0 Step 0.1. See builders-guide.md for the full prompt and review checklist. -->

Full specifications: `docs/phase-0/frd.md`. **Command grammar (amended at Phase 0 review, Karl 2026-08-03 — supersedes the intake's bare single words):** the five voice commands are **"next slide"**, **"previous slide"**, **"pause presentation"**, **"continue presentation"**, **"go to slide N"** (N as digits, number words, or digit-by-digit). Two-word phrases shrink the audience false-trigger surface.

### Must-Have (MVP)

- **F1 — PPTX load & render (text+images tier):** If the user opens a .pptx (File→Open, drag-drop, CLI argument), the system must parse the OOXML package and render every slide — text boxes with formatting, placed images, solid/picture backgrounds — into a pixel-stable full-screen view. Failure state: invalid/corrupt file → error naming the failing part, app stays alive; unsupported element → visible placeholder + triage-shaped load-time warning list (never a silent wrong render); >200MB, >300 slides, or >1GB decompressed (zip-bomb cap, per-part limits) → rejected with the stated limit.
- **F2 — Voice navigation ("next slide" / "previous slide"):** If the always-on recognizer matches either phrase while presenting, the system must move exactly one slide (p95 ≤1.5 s end-of-utterance→action) and echo the command in the overlay. Failure state: at deck edge → no-op + transient overlay notice; low confidence → no action, heard-text shown; mic unavailable → persistent banner, keyboard unaffected.
- **F3 — Recognition control ("pause presentation" / "continue presentation"):** If "pause presentation" matches, command matching suspends (only "continue presentation" is actionable); overlay shows persistent PAUSED state; heard speech is still displayed but never executed. Failure state: repeats are idempotent; engine crash → automatic restart + overlay alert; keyboard pause toggle works with the engine dead.
- **F4 — "Go to slide N" with robust numbers:** If "go to slide <number>" matches and N is in range, jump directly to N; accepted forms: "15", "fifteen", "one five". Failure state: out of range → "deck has M slides" notice, no move; unparseable → heard-text shown, no move.
- **F5 — Live command-transcript overlay with listening-state glyph:** If any utterance is processed, display heard text + matched command (or "no match") within 500 ms, auto-fading ~3 s; transient messages fade, status messages (PAUSED, mic dead) persist; a persistent glyph always shows listening / paused / engine-dead state. Failure state: overlay confined to its reserved strip; overlay failure never takes down the slide view.
- **F6 — Keyboard parity (strict, two-directional):** If arrow keys / space / P / typed-number+Enter are pressed, execute the identical action to the corresponding voice command — and the keyboard surface is exactly the five commands, nothing more (B/blank-screen dropped per Q2). Failure state: keyboard path fully independent of the speech engine.
- **F7 — Presentation mode UI (minimal dark, dual-display, safe exit):** If a presentation starts, render the full-screen minimal-dark view (slide, overlay strip, slide counter), routing slides to the external display when attached. Esc / end-of-show returns to the app's own dark holding screen — never the desktop on the projector; quitting requires confirmation. Failure state: no external display → single-screen mode; mid-show display disconnect → fall back to laptop screen preserving position.

### Should-Have (v1.1)

1. Timer / elapsed clock in the presenter overlay.
2. Practice mode with per-command recognition-accuracy stats.
3. Microphone input-device picker.
4. Presenter-notes view on laptop with clean projector output.
5. Windows/Linux packaging and full validation (code stays portable and CI-built throughout).
6. Blank/black-screen action (voice + key) — moved here when dropped from MVP (Q2).
7. Crash slide-position restore (Q11).
8. Push-to-talk / hold-to-command mode (Q1 residual idea).

### Will-Not-Have

- **Deck editing/creation/export:** out of domain — this is a presentation controller, not an authoring tool.
- **Any runtime network capability:** cloud speech, accounts, telemetry, update phone-home — reliability and confidentiality both forbid it; speech models ship inside the app bundle (no first-run download).
- **Free-form dictation / wake words:** fixed five-phrase grammar only; anything more grows the misfire surface.
- **Animation/transition playback, embedded video/audio:** static slide rendering only; media elements render placeholders.
- **Audio recording or persistence:** audio is processed in memory and discarded; heard-text persists only in the opt-in rehearsal log (Q7).

---

## 3. User Journeys

<!-- Source: Phase 0 Step 0.2. See builders-guide.md for the full prompt and review checklist. -->

Full journey: `docs/phase-0/user-journey.md` (14 steps, 16 failure-recovery rows, skeptical-PM pass).

### Persona

- **Who:** Karl Raulerson, presenter/technologist (secondary: colleague presenters, mixed skill, 6-12 months out)
- **Skill Level:** High technical proficiency; expert on his own deck
- **Goal:** Deliver a live executive presentation hands-free, including direct slide jumps during Q&A
- **Emotional State on Arrival:** Time-pressured and stakes-aware — T−30 minutes in the room; error messages must be calm, specific, and actionable

### Success Path

1. **Pre-show (T−30):** User opens the app, loads the real deck. System renders all slides, reports any unsupported elements in a triage-shaped list; user spot-checks fidelity against PowerPoint.
2. **Voice check (T−20):** User runs the 10-second pre-show voice check; the listening glyph confirms the mic is live; a test "next slide" round-trips on the laptop screen.
3. **Showtime:** User starts presentation mode; slides go full-screen to the projector, overlay strip + counter on. "Next slide" advances through the talk; user walks the room untethered.
4. **Q&A:** User says "go to slide fifteen"; system jumps directly; overlay echoes the command. For open discussion, "pause presentation" freezes matching (PAUSED persistent); "continue presentation" resumes.
5. **End:** Esc returns to the app's dark holding screen (desktop never projected); user quits with confirmation.

### Failure Recovery

- **Step 1:** Corrupt/oversized deck → named error, app alive; unsupported elements → placeholders + warning list; decision to proceed is the user's, made at T−30, not at showtime.
- **Step 2:** Glyph shows engine-dead or check fails → mic-unavailable banner; user presents keyboard-primary from the start (a plan, not a scramble).
- **Step 3:** Misrecognition → heard-text visible in overlay (instant diagnosis); low confidence → no action taken; engine crash → auto-restart + alert, keyboard seamless meanwhile.
- **Step 4:** Out-of-range/unparseable number → notice, no movement, deck position unchanged.
- **Step 5:** Mid-show display disconnect → laptop-screen fallback preserving position; app crash → relaunch, reopen deck, "go to slide N" (two actions, accepted for MVP per Q11).

### Exit Points

- **Abandon at load (fidelity unacceptable):** exit to stock PowerPoint — the deliberate, documented fallback (intake §8.5); nothing lost.
- **Mid-show voice abandonment (noise/nerves):** keyboard parity means the show continues without the app ever being closed.
- **Post-show:** dark holding screen prevents accidental desktop exposure; quit-confirm prevents accidental close while projected.

---

## 4. Data Contracts

<!-- Source: Phase 0 Step 0.3. See builders-guide.md for the full prompt and review checklist. -->

Full contract: `docs/phase-0/data-contract.md` (9 inputs: 3 declared + 6 implied-and-formalized).

### Inputs

- **.pptx file:** Type: OOXML zip. Validation: valid zip w/ ppt/presentation.xml, ≤200MB, ≤300 slides, ≤1GB decompressed with per-part caps. Sensitivity: Confidential.
- **Microphone audio:** Type: PCM stream (~16 kHz). Validation: on-device, in-memory only, never persisted or transmitted. Sensitivity: Confidential.
- **User settings:** Type: JSON. Validation: schema-validated keys, unknown keys rejected, corrupt file → defaults + notice. Sensitivity: Internal.
- **Bundled speech-model files:** Type: binary model assets in app bundle (build-time input). Validation: startup integrity check; failure → mic-unavailable banner + keyboard fallback. Sensitivity: Public (license verified in Phase 1).
- **Keyboard events / CLI deck path / display topology / recent-files list / embedded fonts:** as formalized in the data contract (fonts: Confidential, memory-only, substitution always visible).

### Transformations

- **T1:** .pptx → OOXML parse → in-memory slide model (text runs, images, backgrounds) → rendered slide surface.
- **T2:** Audio frames → on-device recognizer (fixed five-phrase grammar) → match + confidence → command dispatch.
- **T3:** Number-word normalization: "fifteen" / "one five" / "15" → integer, range-checked against deck length.
- **T4:** Command → slide-state change → display update + overlay echo (+ optional event-only debug record).

### Outputs

- **Rendered slide view:** Format: on-screen (projector/laptop). Latency: <200 ms per slide change.
- **Command-transcript overlay:** Format: on-screen text strip + state glyph. Latency: <500 ms after utterance.
- **Session command log:** Format: in-memory; optional debug file records command events/confidence/timings only — heard text only in the opt-in, local, user-deletable rehearsal log (Q7). Latency: N/A.

### Third-Party Data

- **None.** Zero external services; all runtime network I/O is forbidden by contract. There is no degraded-network mode because there is no network mode.

### State

- **Settings + recent-file paths:** Persist (local JSON; until user deletes).
- **Deck content, audio, transcript history:** Ephemeral (memory only; deck re-read per open; audio discarded post-recognition; crash handling must never write deck/audio memory to disk — no default core dumps, per Q8).

---

## 5. MVP Cutline

<!--
  This is a hard line. Features listed above this line ship first.
  Everything below this line goes to the Post-MVP Backlog.
  This cutline governs Phase 2 — features not above this line are not built.
  Do not move items above the line without Orchestrator approval and a recorded decision.
-->

**Above the line (MVP — ships first):**
- F1 — PPTX load & render (text+images tier, from-scratch renderer, zip-bomb caps)
- F2 — Voice navigation: "next slide" / "previous slide"
- F3 — Recognition control: "pause presentation" / "continue presentation"
- F4 — "Go to slide N" with robust number forms
- F5 — Live transcript overlay + listening-state glyph + pre-show voice check
- F6 — Strict keyboard parity (exactly the five commands)
- F7 — Presentation UI: minimal dark, dual-display, safe exit to holding screen

---

**CUTLINE — nothing below this line is built in Phase 2 without Orchestrator approval**

---

**Below the line (Post-MVP — see Section 6):**
- Timer/clock · practice mode · mic-device picker · presenter notes · Win/Linux packaging+validation · blank-screen action · crash-position restore · push-to-talk

---

## 6. Post-MVP Backlog

<!--
  Items here are candidates, not commitments.
  Prioritized by user feedback after launch, not by this document.
  Do not assign sprints or dates to items in this section.
-->

- **Timer/elapsed clock:** justify by Karl wanting pacing feedback after first live use.
- **Practice mode (per-command accuracy stats):** justify by measured rehearsal misfire rate being worth tracking over time.
- **Mic input-device picker:** justify by an external/lapel mic appearing in a future venue.
- **Presenter-notes view (dual-screen):** justify by notes-dependent talks.
- **Windows/Linux packaging + full validation:** justify by a colleague adopter on those platforms (code is portable and CI-built from day one).
- **Blank/black-screen action (voice + key):** justify by presenter demand for the classic B behavior (dropped from MVP per Q2).
- **Crash slide-position restore:** justify by any real mid-show crash occurrence (Q11).
- **Push-to-talk / hold-to-command:** justify by false-trigger incidents surviving the two-word-grammar mitigation (Q1).

---

## 7. Will-Not-Have List

<!-- Source: Phase 0 Step 0.1. See builders-guide.md for the full prompt and review checklist. -->

- **Deck editing/creation/export:** authoring belongs to PowerPoint; this is a controller. Permanent boundary.
- **Runtime network capability of any kind:** offline is a hard product property (reliability + confidentiality), not a temporary gap; speech models ship in-bundle.
- **Free-form dictation / wake words / grammar growth beyond the five phrases:** every added phrase grows the misfire surface the product exists to minimize.
- **Animation/transition/media playback:** static rendering is the fidelity contract; media gets visible placeholders.
- **Audio recording/persistence:** the product listens to execute, never to record.
- **Accounts, telemetry, analytics:** single-user local tool; nothing phones home.

---

## 8. Open Questions

<!-- Source: Phase 0 Steps 0.1–0.3. See builders-guide.md for the full prompt and review checklist. -->

All questions raised by Steps 0.1–0.3 were presented to the Orchestrator and resolved on 2026-08-03; none remain open.

**Q1: Audience speech false-triggering commands (journey risk #3)**
- Context: always-on grammar + room mic during Q&A.
- Options: accept risk w/ pause discipline; push-to-talk; grammar change.
- Decision needed by: Phase 0 gate
- Status: Resolved — Karl changed the grammar to two-word phrases ("next slide", "previous slide", "pause presentation", "continue presentation"); supersedes the intake's single words; push-to-talk recorded as post-MVP.

**Q2: Keyboard "B" with no voice equivalent (FRD C2 / journey GAP-3)**
- Status: Resolved — B dropped entirely; strict two-directional parity; blank-screen → post-MVP (Karl, 2026-08-03).

**Q3: End-of-show/exit control and desktop exposure (journey GAP-2)**
- Status: Resolved — Esc/end-of-deck → app's dark holding screen; quit requires confirmation; scope clarification of F7 (Karl, 2026-08-03).

**Q4: Mic-liveness visibility (journey GAP-1)**
- Status: Resolved — persistent listening-state glyph in overlay + 10-second pre-show voice check; scope clarification of F5 (Karl, 2026-08-03).

**Q5: Speech display while paused (FRD C3)**
- Status: Resolved — display heard text, execute nothing except "continue presentation" (Karl, 2026-08-03).

**Q6: Overlay message classes (FRD C1)**
- Status: Resolved — transient messages fade (~3 s); status messages persist (PAUSED, mic-dead) (Karl, 2026-08-03).

**Q7: Debug-log confidentiality (data contract G-4)**
- Status: Resolved — debug file records command events/confidence/timings only; heard text only in an opt-in, local, Confidential, user-deletable rehearsal log, default off (Karl, 2026-08-03).

**Q8: Crash-dump containment (data contract G-5)**
- Status: Resolved — crash handling must never write deck content or audio buffers to disk; no default core dumps (Karl, 2026-08-03).

**Q9: Zip-bomb defense (data contract G-6)**
- Status: Resolved — ≤1GB total decompressed cap + per-part caps added to F1 validation (Karl, 2026-08-03).

**Q10: Latency contract wording (FRD C4)**
- Status: Resolved — p95 ≤1.5 s end-of-utterance→action; no absolute per-command ceiling (Karl, 2026-08-03).

**Q11: Crash slide-position restore (journey GAP-4)**
- Status: Resolved — declined for MVP; two-action recovery accepted; backlog item (Karl, 2026-08-03).

**Q12: Speech-model licensing (data contract G-2)**
- Status: Resolved — permissive license is a mandatory Phase 1 engine-selection criterion; the framework's license gate enforces it mechanically at Phase 3 (recorded as a Phase 1 obligation, not a Phase 0 blocker).

**Q13: Product name contains "PowerPoint" (Microsoft trademark — Appendix C)**
- Status: Resolved at gate — see Appendix C; internal-use acceptance + rename trigger recorded there pending Sponsor sign-off wording.

---

## Appendix A: Revenue Model & Unit Economics

<!-- Standard+ Track only. Skip for internal tools. Source: Step 0.5. See builders-guide.md for the full prompt and review checklist. -->

**Pricing Model:** None — internal tool, no revenue (intake §7). Completed on Full track for consistency: the product is free internal software with a single commissioning user.

**Per-User Costs:** $0/month — fully local execution; no hosting, no API calls, no storage services. CI runs on GitHub's free tier for public repos.

**Break-Even User Count:** N/A — zero marginal cost, zero revenue; economics cannot become unsustainable.

**Hosting Cost Ceiling:**
- At 1,000 users: N/A (internal ceiling is ≤10 users; distribution is a local binary)
- At 10,000 users: N/A

---

## Appendix B: Orchestrator Competency Matrix

<!-- Source: Step 0.6. See builders-guide.md for the full prompt and review checklist. -->

Karl's self-assessment (2026-08-03, intake §6.2), mapped to this template's domains; two rows derived during Phase 0 and flagged for Sponsor confirmation at the gate.

| Domain | Can I Validate? | If No: Automated Tool |
|---|---|---|
| Product / UX Logic | Yes | — |
| Frontend / UI | Partially | Mandatory Phase 3 tooling: UI test automation + visual checks |
| Backend / API | N/A — no server/API | — |
| Database | Yes (scope: local JSON/file storage) | — |
| Security | Yes | Framework scanners run regardless (Semgrep, gitleaks, Snyk, ZAP N/A-web) |
| Build & Packaging | Yes (derived from DevOps "Yes" — confirm at gate) | CI verification on all target platforms active anyway |
| Accessibility | Partially | Mandatory Phase 3 tooling: platform accessibility audit (keyboard/contrast/screen-reader) |
| Performance | Yes | — |
| Platform-Specific | Partially (derived conservatively — macOS TCC/mic permissions, display APIs; confirm at gate) | Platform-specific test pass on macOS mandatory in Phase 3 |

---

## Appendix C: Trademark & Legal Pre-Check

<!-- Standard+ Track only. Source: Step 0.7. See builders-guide.md for the full prompt and review checklist. -->

**Trademark Search:**
- USPTO search result for "powerpoint-voice": Conflict by construction — "PowerPoint" is a registered trademark of Microsoft Corporation; any name embedding it is unavailable as a mark. No formal USPTO/WIPO search needed to establish this.
- WIPO search result for "powerpoint-voice": Same conclusion — the embedded mark controls.
- Domain availability: Not sought — no domain is needed (desktop tool, GitHub distribution).
- **Disposition (for Sponsor sign-off at the gate):** acceptable as an internal, non-commercial, descriptive project name (nominative use of "PowerPoint" to describe what it controls); **rename is required before any external distribution, commercial use, or marketing** — recorded as a standing trigger in the backlog. The public GitHub repo description will avoid implying Microsoft affiliation.

**Data Privacy Applicability:**
- GDPR applies: No — no EU users (intake §8.4), no personal-data collection or transmission.
- CCPA applies: No — no sale/collection of consumer personal information; single internal user.
- Other regulations: None — no HIPAA/FERPA/COPPA surface; voice audio is processed in-memory on-device and never stored.

**Distribution Channel Requirements:**
- Desktop self-distribution (GitHub Releases): no store review; macOS Gatekeeper will warn on unsigned builds — acceptable for own-machine MVP use; code signing deferred to post-MVP (intake §10), Apple Developer Program recorded in the deferred tooling list.

---

## Appendix D: Market Signal & Go/No-Go Evidence

<!--
  Source: Steps 1.1 / 1.1.5 (builders-guide.md). Required on Standard and
  Full tracks BEFORE committing to architecture; Light track fills the
  SKIPPED line instead. "At least one positive signal" means documented
  evidence someone else can re-fetch — not a gut feeling. The Phase 1→2
  gate checks this appendix exists and is non-placeholder on Standard+
  (WARN-first; check-phase-gate.sh # BL-102-MARKET-SIGNAL).
-->

**Track note:** Full track, internal tool with a single commissioning customer — signals are first-party and re-fetchable in this repository.

### Market signals

| # | Claim the signal supports | Signal type | Source (+ permalink) | Evidence tag | Verification outcome |
|---|---|---|---|---|---|
| 1 | "The commissioning user needs hands-free, offline slide control for a dated live presentation" | Customer interview (recorded product interview + 7 interactive decisions) | PROJECT_INTAKE.md §2-§4 (repo, merged PR #3) | `seen it` | re-fetched 2026-08-03: text-match OK |
| 2 | "The need is dated and funded with committed time" | Letter-of-intent equivalent (sponsor time allocation "as many hours as needed"; hard date ~2026-08-10) | APPROVAL_LOG.md pre-condition row 4 + intake §3.1/§8.1 | `seen it` | re-fetched 2026-08-03: text-match OK |
| 3 | "Adjacent demand exists beyond the single user (colleague adoption)" | Sponsor projection (≤10 internal users at 12 months) | intake §3.3 | `hunch` | recorded as projection, not evidence; cannot carry the Go alone and does not need to |

**Verification counts:** checked: 2 · failed: 0 · dropped: 0
Signals 1-2 are first-party commissioning evidence — appropriate for an internal tool; no external market claim is being made, so no external sweep is required.

### Go/No-Go decision (Step 1.1)

- **Decision:** GO — 2026-08-03
- **Decided by:** Karl Raulerson (Orchestrator/Sponsor), at the Phase 0 → Phase 1 gate ("Approved as proposed")
- **Rationale:** A commissioned internal tool with a dated live-use deadline (~2026-08-10) and committed sponsor time ("as many hours as needed") constitutes the positive signal; signals 1-2 are first-party and re-fetched; the fallback (stock PowerPoint) bounds the downside.
