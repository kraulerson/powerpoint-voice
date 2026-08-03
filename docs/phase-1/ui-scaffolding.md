# UI & UX Scaffolding — powerpoint-voice (Phase 1 Step 1.5)

**Platform:** Qt 6.8 Widgets (ADR-0001). Text-based component specifications, not visual mockups.
Accessibility baseline (intake §9, Competency "Partially"): every interactive element carries
a text/accessible label; never rely on color alone; contrast ≥4.5:1; full keyboard operability
(F6 is a Must-Have). Minimal dark theme is the only theme (Manifesto).

## Core screens / views

### 1. StartView (pre-presentation)
The dark landing surface before a deck is loaded or after exit-to-holding (F7 Q3).
- **Layout:** centered app name; "Open deck…" button; recent-files list; drag-drop target
  covering the whole window; a status line. Nothing is projected from here.
- **Owns:** deck selection (File→Open dialog, drag-drop, CLI arg), recent-files display,
  invoking the load pipeline, showing load warnings.

### 2. LoadReportView (modal over StartView)
Shown after a deck parses, BEFORE entering presentation mode — surfaces F1's unsupported-element
warning list, triage-shaped (the T−30 go/no-go decision point from the user journey).
- **Layout:** slide-count summary; scrollable per-slide warning list ("Slide 9: SmartArt →
  placeholder"); "Enter presentation" (primary) / "Cancel" buttons.
- **Owns:** presenting the LoadWarning[] data-driven list; gating entry to presentation mode.

### 3. PresentationView (the main feature — full-screen, routed to external display)
- **Layout:** the rendered slide fills the screen (QPixmap from the pre-render cache);
  a reserved bottom overlay strip (TranscriptOverlay); a slide counter (e.g. "12 / 34");
  a persistent listening-state glyph (F5 Q4). On external-display present, this view goes
  to the projector; the laptop may show the same or a minimal holding surface.
- **Owns:** slide display, keyboard event capture (F6), routing to QScreen, HiDPI scaling.

### 4. HoldingView (dark exit screen — F7 Q3)
Reached by Esc / end-of-deck. Solid dark screen with a small "Presentation ended — Esc to
exit / any nav command to resume" hint. Guarantees the desktop is NEVER projected. Quit from
here requires confirmation (QMessageBox).

## The two most important component skeletons

### Component A — SlideCanvas (owns the from-scratch render output)
- **Responsibility:** given a slide index, display the pre-rendered QPixmap for that slide;
  own scaling/letterboxing to the display; nothing else (rendering happens off-thread in the
  RenderService, not here — TM-018 mitigation).
- **Four states:**
  - **Empty:** no deck loaded → dark surface + "No deck loaded" (StartView context).
  - **Loading:** deck parsing / slides pre-rendering → dark surface + indeterminate progress
    + "Rendering slides…" (accessible-labeled); UI thread never blocks (TM-018).
  - **Error:** a slide failed to render → the slide's placeholder pixmap + a non-modal
    overlay note "Slide N could not render (reason)"; navigation still works.
  - **Success:** the slide pixmap displayed, crisp at display DPI.

### Component B — TranscriptOverlay (F5: transcript + listening-state glyph)
- **Responsibility:** show heard text + matched command (or "no match"); show the persistent
  state glyph (listening / paused / engine-dead); manage transient-vs-persistent message
  classes (Q6). Read-only; owns no navigation logic.
- **Four states:**
  - **Empty:** presenting, no recent utterance → only the state glyph visible (listening).
  - **Loading:** N/A for a display strip — the recognizer's "thinking" is sub-perceptible;
    if a partial hypothesis exists it shows as dimmed transient text.
  - **Error:** engine-dead → persistent red-AND-icon "Voice engine restarting — keyboard
    active" (never color alone); mic-unavailable → persistent banner.
  - **Success:** matched command echoed as a transient message, auto-fading ~3 s; PAUSED
    shown as a persistent status message while paused.

## Interface note (accessibility + keyboard, F6)

Keyboard map (the entire keyboard surface — strict parity, nothing beyond the five commands,
Q2): →/Space = next slide · ← = previous slide · P = toggle pause presentation · digits then
Enter = go to slide N · Esc = holding/exit. Every on-screen affordance has an accessible name
via Qt Accessibility; the state glyph exposes its state as text to the a11y tree (mandated
Phase 3 accessibility audit will verify with VoiceOver / Accessibility Inspector).

## Architecture note carried into the Bible (from threat model TM-018, availability = top asset)

**Pre-render strategy:** all slides are rendered to in-memory QPixmap on a background thread
at deck-load (bounded by per-slide complexity caps + a hard per-slide deadline that degrades
to a placeholder), NOT lazily during the talk. This removes mid-talk UI-thread stalls (TM-018),
at the cost of load-time work and memory (bounded: ≤300 slides × display-resolution pixmap).
No on-disk render cache (TM-011 / no-deck-content-on-disk) — pre-render is memory-only and
re-run each open. If memory pressure is detected on constrained machines, fall back to a
sliding window (current ± K slides) still rendered off the UI thread.
