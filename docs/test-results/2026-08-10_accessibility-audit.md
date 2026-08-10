# Accessibility Audit — Phase 3.4

**Date:** 2026-08-10
**Persona:** Users with Disabilities (screen reader, keyboard-only, colour-blind / low vision)
**Standard:** WCAG 2.1 AA-equivalent for a native desktop app (PROJECT_BIBLE.md §14, intake §9)
**Commit:** 63fa398
**Platform audited:** macOS 26, Qt 6.11.1, the build shipped to the presenter 2026-08-10

The persona's instruction for this step is *"report specific failures, not missing
attributes."* So this audit reports what a person would actually hit, with measurements, and
it separates **what was fixed** from **what was found and deliberately not fixed**.

---

## 1. Screen reader (VoiceOver)

### Fixed during this phase

The presentation surfaces are custom-painted `QWidget`s. To a screen reader that is an
unnamed rectangle with no text in it, so **the deck, the privacy blackout, the quit prompt and
a hung application were indistinguishable: silence.** There is no menu bar and no on-screen
control, so there was nothing to discover the key map from either.

| Was | Now |
|---|---|
| Presentation window: no name, no description | Named, and its description carries the whole key map — the only way in without a menu bar |
| Slide surface: silent on every change | Announces "Slide 3 of 10.", "Projector blanked…", "Rendering slide 4 of 10." |
| Notice strip: silent | Announces each notice as it appears |

**Two defects were found in that fix and are worth recording, because both passed their first
round of tests:**

- **BUG-80** — the first version raised `QAccessible::Alert` and `DescriptionChanged`. Neither
  has an AppKit equivalent, so Qt's Cocoa plugin **discards both** and VoiceOver says nothing.
  `nm -mu libqcocoa.dylib` gives the complete set the plugin can post: Focused / SelectedText /
  Title / ValueChanged, plus `NSAccessibilityAnnouncementRequestedNotification`. Only the last
  carries a message. The four tests asserted the stored *strings*, which the no-op version set
  perfectly well — so they passed while nothing was ever spoken.
- **BUG-87** — the corrected API (`QAccessibleAnnouncementEvent`) is Qt 6.8+, and did not
  compile on CI's older Linux Qt. Now behind one shared version guard; below 6.8 it is a
  deliberate no-op rather than a fake announcement.

**Deck content is never announced.** "Slide 3 of 10" names the state, never what is on the
slide. The accessibility tree is readable by other processes and the deck is Confidential
(TM-012/013); a test asserts the surface's description does not contain slide content.

### NOT verified — stated plainly

**No end-to-end VoiceOver session has been run.** The tests assert that the correct event
reaches Qt's accessibility bridge, which is one layer below "a person heard it". Verifying the
rest needs a human with VoiceOver on, and that has not happened. **This is the single largest
gap in this audit.**

---

## 2. Keyboard-only operation

**Full operability: PASS.** Every function of this application is reachable from the keyboard,
and that is structural rather than incidental — the keyboard is the guaranteed control path and
nothing may gate it (F8b/F8c audits).

| Function | Key | Verified by |
|---|---|---|
| Next / previous slide | → ↓ Space PgDn / ← ↑ PgUp | `G:` group; human arm 2026-08-10 |
| Jump to slide N | digits then Enter | `G:` group |
| Pause / resume voice | **P** | `W/F-2`, `BUG-81` group; human arm 2026-08-10 |
| Blank the projector | Esc | `D:` group |
| Quit (two-step) | Esc, Esc | `D:` group |
| Quit (immediate) | ⌘Q | `Q/BUG-31` group |
| Move to the other screen | ⌃⇧D | `display_geometry` group |
| Open a deck | ⌘O, or the Open button, or drag-drop | `S/BUG-18` |

**Two keyboard defects were found and fixed in this phase**, both of which would have been
felt by a keyboard-only user first:

- **BUG-73** — the P key was **dead**. `setPaused()` had zero callers, so it never reached the
  gate. Voice was the only way to pause: that is A11Y-2, and it meant a presenter who could not
  or would not use the microphone had no way to gate it at all.
- **BUG-81** — after that fix, **holding** P toggled once per auto-repeat and landed back on
  live. Holding a key rather than tapping it is a normal accommodation for limited fine motor
  control, so this is an accessibility defect as much as a security one.

**Focus indication: FAIL — see finding A11Y-3 below.**

---

## 3. Colour and contrast

Measured, not eyeballed. Ratios computed with the WCAG 2.1 relative-luminance formula.

| Element | Ratio | Requirement | Verdict |
|---|---|---|---|
| Start screen title `#f2f3f5` on `#101114` | **17.00:1** | 4.5:1 | PASS |
| Start screen hint `#9aa0a6` on `#101114` | **7.15:1** | 4.5:1 | PASS |
| Button label `#f2f3f5` on `#2b2f36` | **12.10:1** | 4.5:1 | PASS |
| Notice text `(235,235,235)` on the strip | **17.62:1** | 4.5:1 | PASS |
| Surface status text `(180,180,180)` on black | **10.13:1** | 4.5:1 | PASS |
| **Button border `#454b54` on `#101114`** | **2.15:1** | **3:1** (SC 1.4.11) | **FAIL** |
| **Button fill `#2b2f36` on page `#101114`** | **1.40:1** | **3:1** (SC 1.4.11) | **FAIL** |
| **Missing-image placeholder `(32,32,32)` on black** | **1.29:1** | **3:1** (SC 1.4.11) | **FAIL** |

**Colour is never the sole carrier of meaning.** Every state — voice unavailable, paused,
rendering, blanked, missing image — is carried by *text*, not by a colour change. Checked
against the full `NoticeId` vocabulary in `src/present/notice.cpp`: all eleven cases produce a
sentence. A colour-blind user loses nothing.

---

## Findings

| ID | Severity | Finding |
|---|---|---|
| **A11Y-3** | SEV-3 | **No visible focus indicator on the Open button.** `start_view.cpp` sets a stylesheet with `:hover` but no `:focus`, and applying a stylesheet to a QPushButton suppresses the native focus ring. A keyboard-only user tabbing to the button gets no indication it is focused. Bible §14 requires visible focus states explicitly |
| **A11Y-4** | SEV-3 | **The Open button's boundary is below the 3:1 non-text contrast minimum** — border 2.15:1, fill 1.40:1 against the page. The label is highly legible (12.10:1); it is the *control's shape* that a low-vision user cannot locate |
| **A11Y-5** | SEV-4 | **The missing-image placeholder is 1.29:1 against the surface.** It is meant to be a visible "this did not render" marker and a low-vision user will not see it. Mitigated in part by the diagonal cross being a shape rather than a colour |
| **A11Y-6** | SEV-3 | **No end-to-end VoiceOver verification.** Tests confirm the right event reaches Qt's bridge; nobody has confirmed a person hears it |

---

## Not remediated in this build, and why

**All four findings are recorded and none is fixed in the shipped build.** That is a
deliberate call, not an oversight.

The build on the presenter's Desktop was human-tested and passed on 2026-08-10. The talk is
2026-08-12 and the presenter is unavailable on 2026-08-11. **Any code change invalidates that
human verification and would need the whole check re-run**, and the four findings above are:

- not on the presentation path at all (A11Y-3, A11Y-4 are on the start screen, used once,
  before the talk begins),
- cosmetic under the conditions this build will actually run in (A11Y-5),
- or not fixable by a code change (A11Y-6 needs a human with VoiceOver).

Trading a verified build for an unverified one, two days out, to improve a start-screen focus
ring is the wrong trade. **They are filed as BUGS.md rows for post-talk, not waived.**

---

## Summary

| | Count |
|---|---|
| Requirements met | Keyboard operability (full), colour-not-sole-carrier, text contrast (5 of 5 text pairs) |
| Fixed this phase | 4 — A11Y-1, A11Y-2, and the two defects found inside the A11Y-1 fix (BUG-80, BUG-87) |
| Open findings | 4 — A11Y-3, A11Y-4, A11Y-5, A11Y-6 |
| Blocking the talk | **None** |
