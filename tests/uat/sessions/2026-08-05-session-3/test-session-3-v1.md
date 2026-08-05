# UAT Test Session — 3

**Date:** 2026-08-05
**Features Under Test:** F7a (presentation funnel), F7b (usable presenter)
**Tester:** Karl Raulerson (human) + 5 parallel agent-testers

> **This is the first session where the product actually presents.** UAT-1 tested the loader and
> renderer as a library; UAT-2 tested the command grammar. This session tests **the application** —
> you open your real deck and drive it. That is also exactly where the last audit found every one of
> its five Critical defects: in the wiring between well-tested parts.

---

## Before you start

- **System under test:** macOS (Apple Silicon) — the showtime machine. Ubuntu validated via CI.
- **Build:**
  ```
  cd "/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice"
  export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
  export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
  export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
  ```
- **Run it on your real deck** (it never leaves your machine; nothing is uploaded, nothing is
  written to disk, and I do not open the rendering — you are the eyes):
  ```
  ./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice "../Solo Orchestrator - FirstService IT Summit.pptx"
  ```

**Controls**

| Key | Does |
|---|---|
| `→` `Space` `↓` `PgDn` | next slide |
| `←` `↑` `PgUp` `Backspace` | previous slide |
| digits then `Enter` | jump to that slide (expires after 3 s if you pause) |
| `P` | pause / resume voice (voice is not wired yet — this is a no-op for now) |
| `Esc` | privacy blackout (hides your deck) |
| `Esc` again | quit prompt |
| `Ctrl+Shift+Q` | actually quit (only from the prompt) |
| `Ctrl+Shift+D` | move the window to the next screen (for the projector) |
| `Ctrl+Shift+F` | toggle fullscreen |

---

## Scenarios — HUMAN (Karl)

### A. The thing that matters: does your deck present?

| # | Scenario | Expected | Pass/Fail | Notes |
|---|---|---|---|---|
| 1 | Open your real deck | It opens and slide 1 appears fullscreen. First render may briefly say "Rendering slide 1..." | _pending_ | |
| 2 | Page through all 10 slides with `→` | Every slide appears, correct order, correct content, no blank/black frames | _pending_ | **The core fidelity check** |
| 3 | Page back with `←` | Same slides in reverse; stops at slide 1 | _pending_ | |
| 4 | Compare against PowerPoint | Text readable and correctly coloured/sized; images present; layout close enough to present with | _pending_ | Note ANY slide that looks wrong |
| 5 | Type `7` then `Enter` | Jumps to slide 7 | _pending_ | |
| 6 | Type `99` then `Enter` | Does NOT move; says "Deck has 10 slides" | _pending_ | Must never jump to a wrong slide |
| 7 | Type `4`, wait ~5 s, type `2`, `Enter` | Goes to slide **2**, not 42 (the stale digit expires) | _pending_ | |

### B. The safety behaviours (these protect the talk)

| # | Scenario | Expected | Pass/Fail | Notes |
|---|---|---|---|---|
| 8 | Press `Esc` | Screen goes **dark and your deck is hidden**, with a hint line | _pending_ | Privacy: this must genuinely blank |
| 9 | Press `→` from the blackout | Returns to the deck and advances | _pending_ | |
| 10 | `Esc`, `Esc` | A **visible** quit prompt appears | _pending_ | |
| 11 | From the prompt, press `→`, `Space`, `Enter`, digits | **Nothing happens** — the deck must not move behind the prompt | _pending_ | |
| 12 | Wait ~10 s at the prompt without pressing anything | It disappears by itself, back to the blackout | _pending_ | |
| 13 | `Esc`,`Esc`, then `Ctrl+Shift+Q` | The app quits | _pending_ | The ONLY way to quit |
| 14 | Try to close the window (red button / `Cmd-W`) while presenting | **Refused** — the window stays | _pending_ | A stray click must not end your talk |

### C. Projector / second display (do this with your actual presenting setup if you can)

| # | Scenario | Expected | Pass/Fail | Notes |
|---|---|---|---|---|
| 15 | Plug in the projector/second display, then open the deck | The slide goes fullscreen on the **external** display | _pending_ | |
| 16 | If it opened on the wrong screen, press `Ctrl+Shift+D` | It moves to the other screen | _pending_ | The escape hatch |
| 17 | Unplug the display mid-presentation | The app survives; the deck reappears on the laptop | _pending_ | Report exactly what happens |

### D. Nice-to-know (report anything that annoys you)

| # | Scenario | Notes |
|---|---|---|
| 18 | How long from launch to slide 1 being visible? | _pending_ |
| 19 | Does advancing feel instant? | _pending_ |
| 20 | Anything that looks unprofessional if an audience saw it | _pending_ |

---

## Scenarios — AGENT (automated, 5 testers)

| # | Area | Pass/Fail |
|---|---|---|
| 21 | Full suite, flakiness, ASan/UBSan, ThreadSanitizer, Semgrep | _pending agent_ |
| 22 | Hostile decks: zip bombs, traversal, malformed XML, image floods, peak RSS | _pending agent_ |
| 23 | Presenter flow driven programmatically (the wiring — where the last 5 Criticals lived) | _pending agent_ |
| 24 | Regression: verify all 11 previously-fixed audit findings actually hold | _pending agent_ |
| 25 | Cross-platform: Ubuntu CI, the new widget test binary headless, clang-tidy, LFS | _pending agent_ |

---

## Bugs Found

_To be consolidated after both the agent results and Karl's run (see BUGS.md)._

## Overall Notes

_To be completed at triage._
