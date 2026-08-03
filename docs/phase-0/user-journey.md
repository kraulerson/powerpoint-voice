> **POST-REVIEW AMENDMENT (2026-08-03, binding):** At Phase 0 review Karl resolved all flagged questions and CHANGED THE COMMAND GRAMMAR to two-word phrases: "next slide", "previous slide", "pause presentation", "continue presentation" (plus "go to slide N" unchanged). Bare single-word commands in this document are superseded. The B key / blank-screen is DROPPED from MVP (strict five-command parity). Authoritative resolutions: PRODUCT_MANIFESTO.md §8 (Q1-Q13).
# User Journey Map — powerpoint-voice

<!--
  Phase 0 Step 0.2 output. This document captures the full user journey analysis
  before it is summarized into the Product Manifesto Section 3.

  Agent persona for this step: Skeptical Product Manager.
  Every step below was challenged: nervous presenter, mid-sentence interruptions,
  far-from-mic positions, live executives watching every fumble.

  Source of truth: PROJECT_INTAKE.md §2.1 (problem), §2.2 (personas),
  §4.1 (7 Must-Have features + failure states), §8.5 (exit criteria).
  Command set is fixed: "next" / "previous" / "pause" / "continue" /
  "go to slide N" — plus keyboard equivalents. Fully offline. Nothing else.
-->

**Date:** 2026-08-03
**Status:** Draft

---

## Primary Persona

| Field | Value |
|-------|-------|
| **Name** | Karl Raulerson |
| **Role** | Presenter / technologist — Orchestrator and sole launch user (Intake §2.2, §3.3) |
| **Goal** | Deliver a live executive presentation hands-free: walk the room, say "next" / "go to slide fifteen", and the projector obeys — with a keyboard fallback for every command if voice degrades |
| **Context** | Conference room, ~2026-08-10, no trustworthy Wi-Fi (fully offline is a hard constraint), built-in MacBook mic at room distance (Intake §11 risk 7), external projector, live executives. The showtime machine is also the dev machine, so rehearsal (2026-08-08) has already exercised this exact hardware — a precondition this journey depends on and the secondary persona does NOT get |
| **Technical Skill** | High — but skill is irrelevant mid-show: at T-0 he is a nervous presenter with zero attention to spare for debugging. The app must behave as if the user were non-technical |

---

## Journey: Live Executive Presentation — T−30 Minutes Through End of Show

### Entry Point

Karl walks into the conference room ~30 minutes before the executives arrive. Laptop under one arm, deck file already on local disk (Confidential — it never leaves the machine, Intake §5.1). The projector cable may or may not be connected yet. He is time-pressed, adrenalized, and splitting attention between room setup, AV, and mental rehearsal. He launches the app from the Dock (colleagues may use File→Open or drag-drop; Karl may use the CLI argument — all three are Feature 1 entry routes).

**Skeptical framing:** everything in the next 30 minutes is triage, not exploration. Any step that requires reading documentation, interpreting an ambiguous state, or debugging has already failed. Every system response below must be legible to a distracted person in under 3 seconds.

### Success Path

Each step lists what Karl sees, what he does, how the system responds, and the feedback mechanism that proves it worked. Feature numbers refer to Intake §4.1.

| Step | User Sees (Context) | User Action | System Response | Feedback Mechanism / Success Criteria |
|------|--------------------|-------------|-----------------|--------------------------------------|
| 1. Launch (T−30) | Dock icon; dark minimal start screen with recent-files list (paths only, Intake §5.4) | Opens the app | App window appears; recent list shows the deck from rehearsal | Window visible in <2 s; the deck he rehearsed is at the top of the recent list — instant reassurance this is the same build/config that worked on 08-08 |
| 2. Load deck (F1) | Start screen | Drag-drops the .pptx (or clicks it in recents / File→Open) | Parses the OOXML package; renders every slide (text+images tier); shows slide 1 full-window with slide counter "1 / M" | **Load report is explicit in both directions:** either "0 unsupported elements" or a warning list naming every unsupported item and its slide number. Silence is never the success signal — absence of a warning must be a positive statement, not a missing dialog (a failed warning system would otherwise look identical to a clean load) |
| 3. Fidelity spot-check (F1 + F6) | Slide 1, counter, warning list (if any) | Arrows through the deck with the keyboard, checking slides against what he rehearsed; jumps via typed-number+Enter to any slide named in the warning list | Each slide renders pixel-stable in <200 ms (Intake §5.2); unsupported elements show visible placeholders — never a silent wrong render | Slide counter tracks position; placeholders are visually unmistakable. Success: Karl can judge in ≤2 minutes whether any warning is content-critical. This is the last moment the §8.5 fallback (stock PowerPoint) is cheap — the warning list must make that judgment fast, not force a 40-slide hunt |
| 4. Projector connect (F7) | AV staff plug in HDMI | Connects external display | Detects the display; routes the slide view to it; laptop keeps the control view (slide + overlay strip + counter — no presenter-notes view in MVP, deferred to v1.1) | The slide appears on the projector without Karl touching display settings. Success: correct screen on the correct output, no mirroring surprises, no desktop exposed |
| 5. Voice readiness check (F2 + F5, pre-audience) | Room still empty | Starts presentation mode briefly and speaks "next", then "previous" | Recognizer (always-on while presenting) matches; slides move; overlay shows heard text + matched command within 500 ms | The overlay IS the mic check — there is no dedicated one (see GAP-1). Success: two consecutive correct matches at his intended standing distance, not at the laptop. **Skeptical:** if he skips this step, the first-ever proof that voice works happens in front of executives |
| 6. Showtime — start (F7) | Executives seated | Starts the presentation (keyboard or already in mode from step 5, returned to slide 1 via "go to slide one" or typed 1+Enter) | Full-screen minimal-dark view: slide, reserved overlay strip, slide counter; projector shows slides | Slide 1 on the projector, counter reads "1 / M", overlay strip present but unobtrusive (never covers more than the reserved strip — F5 failure state) |
| 7. Deliver — advance (F2 + F5) | Mid-talk, standing away from the laptop | Says "next" between sentences | Advances exactly one slide within 1.5 s; overlay shows «heard: "next" → NEXT», auto-fades ~3 s | The slide change itself plus the overlay echo. The ≤1.5 s p95 latency budget (Intake §2.3) is a pacing contract: Karl learns at rehearsal to breathe, say the command, breathe — the room never waits awkwardly |
| 8. Revisit a chart (F2) | Exec interrupts: "go back to that chart" | Says "previous" (twice if needed) | Rewinds one slide per command; overlay echoes each | Overlay confirms each step; counter shows position. Repeated commands are the design for multi-slide moves — or he names the target directly (step 9) |
| 9. Q&A direct jump (F4) | Exec: "can we see the numbers slide again?" — it's slide 15 | Says "go to slide fifteen" (also valid: "…slide one five", "…slide 15" spoken as digits) | Jumps directly to slide 15; overlay shows «heard: "go to slide fifteen" → GO TO 15» | Slide 15 on the projector without walking to the laptop — this is the core §2.1 promise. Counter reads "15 / M" |
| 10. Discussion breaks out (F3) | Open multi-person discussion; room audio is now adversarial (audience words like "next quarter" could match the grammar) | Says "pause" | Suspends all command matching except "continue"; overlay shows a **persistent** PAUSED indicator (must not auto-fade — a faded indicator plus an ignored "next" reads as a broken app to a presenter who forgot the state) | PAUSED visible at a glance for the whole discussion. Saying "pause" again is idempotent — no error, still paused |
| 11. Resume (F3) | Discussion winds down | Says "continue" | Resumes full command matching; overlay confirms LIVE state; PAUSED indicator clears | Overlay explicitly confirms listening has resumed. **Skeptical:** the failure mode is Karl believing he resumed when he didn't (or vice versa) — the state change itself must be the feedback, not inferred from a later command working |
| 12. Final slide (F2 failure state as designed behavior) | Last content slide | Says "next" out of habit | No-op; overlay notice "last slide (M / M)" | Deck cannot run off the end; the notice tells him why nothing moved — a silent no-op here would read as voice failure at the worst moment |
| 13. End of show | Closing remarks done | **GAP — see GAP-2.** No defined control ends the presentation: Esc is not among the enumerated keys, and no end-of-deck / blank state exists in §4.1 | — | Interim recovery until the FRD resolves GAP-2: leave the final slide displayed, disconnect the projector cable, then exit full-screen. Exiting first would project the desktop — and possibly Confidential material — to the room |
| 14. Exit | Room emptying | Quits the app | Settings and recent-files list persist (paths only); deck content is re-read from the .pptx next open; transcript history discarded; no audio was ever stored (Intake §5.4, §2.4) | Nothing to save, nothing to lose: the app is a read-only viewer of the user's own file. Next launch resumes from the recent list |

### Failure Recovery

Every failure state from Intake §4.1 is mapped to the step where it strikes, plus failures the skeptical pass surfaced beyond the Intake list.

| Failure Point | What Goes Wrong | Recovery Path | User Sees |
|--------------|----------------|---------------|-----------|
| Step 2 — corrupt/invalid file | The .pptx fails OOXML parsing (truncated copy, bad export) | Error names the failing part; app keeps running; Karl re-copies or re-exports the file. If unrecoverable at T−30: §8.5 fallback — stock PowerPoint + keyboard | Error dialog naming the failing package part — never a crash, never a blank window |
| Step 2 — unsupported element discovered at load | The deck contains an element outside the text+images tier (the §11 risk-1 renderer bet landing at the worst time) | Load-time warning lists **every** unsupported item with its slide number; placeholders render visibly. Karl spot-checks exactly those slides (step 3). Content-critical → abandon to stock PowerPoint NOW, while it costs 2 minutes, not mid-show. Cosmetic → proceed, forewarned | Warning panel: element type + slide number per item; visible placeholder on the affected slide — never a silent wrong render |
| Step 2 — oversize deck | File >200 MB or >300 slides | Rejected with the stated limit; Karl splits or re-exports the deck | Clear rejection naming the limit and the deck's actual size/count |
| Step 3 — font substitution (Intake §11 risk 3) | Deck uses embedded/custom fonts the renderer substitutes | Substitution is visible at spot-check, never silent; Karl judges fidelity against the §2.3 zero-defect criterion; fails → §8.5 fallback | A visible substitution notice (which fonts, which slides) rather than silently different text layout |
| Step 5/7 — mic unavailable or dies mid-show | OS revokes/loses the input device; hardware fault; recognizer receives no device | Persistent banner appears; keyboard path is fully independent of the speech engine (F6) and unaffected. Karl walks to the laptop and finishes keyboard-primary — §2.3 explicitly counts this as graceful degradation, not failure | Persistent "microphone unavailable" banner; slides and keyboard keep working exactly as before |
| Step 5 — mic present but silently useless (GAP-1) | Wrong input device selected, zero gain, OS-muted: the engine runs but hears nothing. No Intake failure state fires — "unavailable" banner never triggers because the device exists | Only detection today: the step-5 readiness check produces no overlay echo → Karl fixes input in OS settings (no in-app picker until v1.1) or commits to keyboard-primary before the audience arrives | Nothing — that is the problem. Silence is ambiguous between "listening, room is quiet" and "deaf". Flagged as GAP-1 |
| Step 7 — misrecognition streak | Room acoustics/nerves/distance: commands repeatedly low-confidence or mis-heard (§11 risk 7) | Low-confidence → no action, overlay shows what was heard (never a guessed action). Karl reads the overlay diagnosis: adjust (closer, slower, louder) — one retry. Two consecutive failures → stop negotiating with the recognizer in front of executives and switch to keyboard; keep presenting. The overlay makes the cause visible in-glance; the 2-strike rule belongs in rehearsal training | Overlay: «heard: "…" → no match» per attempt — instant diagnosis without breaking stride |
| Step 7 — audience speech false-triggers a command | Always-on grammar + room mic: an audience "next" or a number phrase matches and moves the deck mid-answer | Overlay shows exactly what matched and why the slide moved; Karl says "previous" (or arrows back) and moves on. Prevention: "pause" during open discussion — but note the tension: pause also disables "go to slide N", the very command Q&A needs. During Q&A Karl must choose exposure or capability; rehearsal decides his default | Overlay: «heard: "…next…" → NEXT» — the wrong slide, but an explained wrong slide, correctable in ~2 s |
| Step 7/10 — recognizer engine crash | Speech engine dies mid-show | Automatic engine restart with an overlay alert; keyboard pause toggle and all keyboard commands work regardless of engine state (F3/F6) | Overlay alert that the engine restarted; slides never blink; keyboard never hiccups |
| Step 9 — out-of-range N | "Go to slide fifty" in a 40-slide deck | No movement; overlay states the deck size; Karl says the correct number | Overlay: "deck has 40 slides" — the correction is in the message itself |
| Step 9 — unparseable number | Recognizer hears an ungrammatical number phrase | No movement; overlay shows the heard text; Karl retries with digit-by-digit form ("one five" — designed for exactly this) or types number+Enter | Overlay shows heard text, no match — and the retry path is a documented command form, not a workaround |
| Step 10/11 — state confusion | Karl forgets whether he paused; repeated "pause"/"continue" | Both are idempotent — no error, no toggle-flip surprise; persistent PAUSED indicator (step 10) is the ground truth readable at a glance | PAUSED indicator present or absent — one glance resolves the doubt |
| Any step — display unplugged mid-show (F7) | Projector cable kicked out / AV failure | Falls back to the laptop screen without crashing or losing position; Karl turns the laptop to the room or waits for AV to replug; on reconnect, routing restores | Slide continues on the laptop at the same position; no dialog to dismiss mid-sentence |
| Any step — overlay rendering failure (F5) | The overlay subsystem itself fails | Overlay failure must never take down the slide view; presentation continues; voice feedback is lost so Karl drops to keyboard (where the slide change itself is the feedback) | Slides keep rendering; missing overlay is the (degraded) signal to stop trusting voice |
| Any step — full app crash mid-show | Process dies with executives watching | Relaunch (seconds); deck is top of recent list; "go to slide N" or typed-number+Enter returns to position. Position is NOT auto-restored — Karl must remember his slide number (see GAP-4). Keyboard-primary from there is the §2.3-sanctioned degradation | Restart, two actions (open recent, jump to N), resume. Rehearsal should include one deliberate kill-and-recover drill |
| First run on a fresh machine (secondary persona) | macOS mic permission prompt never granted / denied at first launch | Denied permission lands in the mic-unavailable path: persistent banner + fully functional keyboard; recovery is OS Settings → grant → relaunch | Permission prompt at first presentation start; if denied, the persistent banner and untouched keyboard path |

### Feedback Loops

The command-transcript overlay (F5) is the journey's central feedback organ — every utterance produces «heard text → matched command | no match» within 500 ms, auto-fading ~3 s. It closes three loops:

1. **Action confirmation** — the slide changed *and* the overlay says why. Karl never wonders whether voice or coincidence moved the deck.
2. **Failure diagnosis** — on no-action, the overlay shows what *was* heard, so Karl knows instantly whether to repeat, rephrase ("one five"), or abandon to keyboard. A no-op without a reason is indistinguishable from a dead app; this journey requires every no-op to display its cause (last-slide notice, deck-size notice, no-match echo).
3. **State visibility** — persistent (non-fading) indicators for the two dangerous latent states: PAUSED and mic-unavailable. Latent state that only lives in the presenter's memory will be wrong within minutes under stage adrenaline.

Two loops are asymmetric by design, and one is missing: keyboard actions confirm via the slide change itself (no overlay dependency — F6 independence), which is correct; but there is **no positive "listening" idle indicator** — the system signals when the mic is *gone*, never that it is *live and hearing*. That missing loop is GAP-1.

Accessibility note (Intake §9): every overlay/banner state pairs text with position/icon — never color alone; overlay contrast ≥4.5:1 on the dark theme.

### Exit Points

- **Normal exit (step 14):** quit after the show. Settings + recent-files (paths only) persist; transcript history and all audio are ephemeral by design (Intake §5.4). Nothing the user made can be lost — the app never modifies the deck; "work saved" is structurally guaranteed.
- **Degraded exit (mid-show, voice abandoned):** not an app exit — Karl finishes keyboard-primary. §2.3 defines this as graceful degradation. The app must make this exit *cheap*: no mode switch, no dialog, keyboard always live.
- **Terminal exit (abandonment):** quit the app, open stock PowerPoint with the same untouched .pptx (§8.5 fallback). Cost of abandonment is deliberately near-zero at load time (step 2/3) and rises steeply once the show starts — which is why the load report and spot-check must front-load the go/no-go decision to T−30.
- **Unsafe exit (GAP-2):** exiting full-screen while the projector is connected exposes the desktop to the room. Until resolved, the interim procedure is cable-out-first (step 13).
- **Resume:** relaunch → recent list → "go to slide N". No session-position restore (GAP-4), but recovery is two actions.

### Abandonment Risks

| Risk | Moment | Why He'd Walk Away | Recovery / Mitigation |
|------|--------|--------------------|-----------------------|
| Scary warning list at T−30 | Step 2 | A long unsupported-elements list under time pressure reads as "this app can't be trusted", triggering premature fallback even when every item is cosmetic | Warning list must be triage-shaped: per-item slide number and element type, so 2 minutes of spot-check (step 3) turns panic into a judgment. This is the §11 risk-1 renderer bet paying out or failing — surfaced here, decided here |
| Rehearsal miss (planned abandonment) | 2026-08-08 | Recognition <90-95% or rendering defects at rehearsal | §8.5 pre-plans this: conditional = keyboard-primary with voice assist; failure = stock PowerPoint. Because the fallback is pre-decided, a rehearsal miss is a branch, not a crisis |
| Mid-show voice distrust | Steps 7-11 | One public misrecognition makes a nervous presenter abandon voice for the rest of the show — and if abandoning voice were disruptive, he'd abandon the app | The keyboard path is zero-switch-cost and always live (F6), so abandoning *voice* never means abandoning the *app*. Voice can be re-tried at the next natural break (overlay shows it matching again) |
| Secondary-persona first-show flameout | Colleague's first real use | No rehearsal habit, no 2-strike rule, possible unfamiliar mic permission prompt mid-setup, no practice mode until v1.1 | Journey survives on F6 + overlay legibility alone; but adoption realistically depends on the v1.1 practice mode — noted for the Should-Have priority discussion, not added to MVP |

### Feature Gaps Flagged (Not Silently Added)

Per the expansion mandate: flagged for Orchestrator decision — **none of these are added to the MVP; the 7 Must-Haves stand as written.**

- **GAP-1 — No positive mic-liveness / listening indicator (and no pre-show voice check).** §4.1 defines the *negative* signal only (banner when the mic is unavailable). A mic that is present but deaf — wrong device, zero gain, OS-muted — produces no failure state at all, and the input-device picker is deferred to v1.1. The only detection is the step-5 workaround: enter presentation mode early and speak test commands at the overlay. The first *designed* proof that voice works can otherwise occur in front of executives. Decision needed: is a "listening / level" indicator in the overlay strip in-scope for F5/F7, or an accepted rehearsal-procedure mitigation?
- **GAP-2 — No defined end-of-show or exit-presentation control.** Esc is absent from F6's enumerated keys; no end-of-deck state or blank/black screen exists; exiting full-screen with the projector live projects the desktop (and potentially other Confidential material) to the room. Decision needed in the FRD: define the exit control and an end-of-show/blank state — or document cable-out-first as the operating procedure.
- **GAP-3 — Keyboard "B" has no corresponding voice command (F6 contract inconsistency).** F6 promises every listed key executes "the identical action to the corresponding voice command", but the command set is exactly next/previous/pause/continue/go-to-N. Arrows/space→next/previous, P→pause-toggle, typed-number+Enter→go-to-N all map; **B maps to nothing.** If B is the conventional blank-screen, that is an action outside the fixed grammar with no voice equivalent — and blank-screen overlaps GAP-2. Needs FRD resolution: define B's action explicitly or remove it.
- **GAP-4 (minor) — No slide-position restore after an app crash.** Existing features already give a two-action recovery (recent list + go-to-N), so this is flagged as a resilience observation, not a proposed MVP addition. A rehearsal kill-and-recover drill is the zero-cost mitigation.

---

## Secondary Personas (if applicable)

**Colleague presenter (potential adopter, 6-12 months, internal only — Intake §2.2, §3.3).** Mixed technical skill. The journey shape is identical (steps 1-14), but every assumption Karl gets for free is absent, so the same flow must survive these deltas:

| Delta vs. Karl | Consequence | What the Journey Requires |
|---|---|---|
| First run on an unfamiliar (or un-rehearsed) machine | macOS mic-permission prompt appears during setup, possibly at T−5; denial looks like a broken app | The mic-unavailable banner + untouched keyboard path (F6) must carry the entire first show if permission goes wrong; the banner must say *what to do*, not just what's wrong |
| No CLI, no rehearsal discipline, no 2-strike rule | Discovers misrecognition behavior live; may fight the recognizer in front of a room | Overlay legibility (F5) is the only teacher present; the no-match echo must make "switch to keyboard" the obvious next move without training |
| Mixed technical skill | Cannot debug audio devices, display routing, or file errors | Every failure state above must resolve through in-app messages alone — the F1 error naming the failing part, the F7 automatic display fallback, the deck-size notice. Any failure whose recovery is "open a terminal" fails this persona |
| No investment in the tool | One bad first experience ends adoption permanently | Practice mode + mic picker (v1.1 Should-Haves) are the real adoption gate; MVP journey is Karl-viable, colleague-survivable, not colleague-delightful — flagged for the v1.1 prioritization discussion |

No other personas: single-presenter product; the audience and AV staff are environment, not users.

---

## Review Checklist

- [x] Primary persona is specific (not "users") — Karl Raulerson, named, with context, skill, and showtime-machine precondition
- [x] Success path covers the complete flow (entry to exit) — T−30 launch through deck load, spot-check, projector, readiness check, delivery, Q&A jump, pause/resume, end-of-deck, exit (steps 1-14, all 7 Must-Haves exercised)
- [x] At least 3 failure points are identified with recovery paths — 16 mapped, covering every §4.1 failure state plus 4 surfaced beyond the Intake (silent-deaf mic, audience false-trigger, app crash, first-run permission)
- [x] Feedback loops are defined (user knows their action worked) — overlay as central organ; every no-op displays its cause; persistent indicators for latent states; missing positive-liveness loop flagged (GAP-1)
- [x] Exit points preserve user state — settings/recents persist; the app never modifies the deck, so user work is structurally safe; audio never persisted
- [x] Journey was reviewed with Skeptical PM mindset — 4 feature gaps flagged (not added), abandonment economics analyzed, pause-vs-goto Q&A tension surfaced, every success signal required to be positive and legible in ≤3 s
