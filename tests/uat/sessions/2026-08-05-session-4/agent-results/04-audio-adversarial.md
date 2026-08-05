Full run complete: 64 passed, 4 failed. Here are the results.

---

# UAT‑4 — presenter's live experience, driven through the real widgets

**Harness** (all under `/private/tmp/claude-501/-Users-karl-Documents-Claude-Projects-powerpoint-voice/05a4d2ab-f2bc-4839-9c01-2ee22b3c3560/scratchpad/uat4/`): 7 executables that construct the real `AppShell` / `PresentationWindow` / `StartView` and drive them with `QTest::keyClick`, plus two real‑platform (cocoa) probes. Synthetic decks only (`fx/make.py` builds 1/3/10/47‑slide decks from the repo's own fixture generator). **The real deck was never opened, rendered, or read.**

| file | what it does |
|---|---|
| `harness/presenter.cpp` | 14 scenarios, full presenter session (64 pass / 4 fail) |
| `harness/probe2.cpp` | Esc semantics, changed‑PR fixtures, 1‑slide deck, second‑deck landmine, key hammering |
| `harness/probe3.cpp` | 1470‑combination brute‑force key sweep; Cmd+O reachability mid‑talk |
| `harness/probe4.cpp` | modifier‑gate matrix (pure + through the real window) |
| `harness/driver.cpp` | faithful `main.cpp` clone that really calls `exec()` — proves the app terminates |
| `harness/keyprobe2.mm` | measures Qt 6.11's macOS `NSEventModifierFlagNumericPad` mapping |

Build: `cmake -S <scratch>/harness -B <scratch>/hb -G Ninja && cmake --build <scratch>/hb`

---

## FINDINGS

### F1 — SEV‑1 — The arrow keys are dropped on macOS, because every key gate compares `mods == Qt::NoModifier`

`src/present/key_translator.cpp:123` — `if (mods != Qt::NoModifier) { return a; }` — plus the same equality at lines 81 (Esc), 90 (digits) and 106 (Enter). Any modifier bit at all, including `Qt::KeypadModifier`, makes the key vanish: not consumed, no command, no notice.

**VERIFIED** — the gate drops 10 of 12 keys when `KeypadModifier` is set (`hb/probe4`):

```
      key                | NoModifier | KeypadModifier
      Right (advance)    | command    | IGNORED       <-- DIFFERS
      Left (retreat)     | command    | IGNORED       <-- DIFFERS
      Down               | command    | IGNORED       <-- DIFFERS
      Up                 | command    | IGNORED       <-- DIFFERS
      PageDown           | command    | IGNORED       <-- DIFFERS
      Space              | command    | IGNORED       <-- DIFFERS
      digit 5            | consumed, no action | IGNORED       <-- DIFFERS
      Escape             | ui-request | IGNORED       <-- DIFFERS
```

**VERIFIED** — through the real window, on a real 10‑slide deck, a `KeypadModifier` Right does not move the deck:

```
=== K2 — the same, through the REAL window and a REAL deck
      . Right     with KeypadModifier: 'Already at the first slide' -> 'Already at the first slide'  NO MOVEMENT
      . Down      with KeypadModifier: 'Already at the first slide' -> 'Already at the first slide'  NO MOVEMENT
```

**VERIFIED** — Qt 6.11 on macOS turns `NSEventModifierFlagNumericPad` into `Qt::KeypadModifier` and nothing else (`hb/keyprobe2.app/Contents/MacOS/keyprobe2`, real cocoa platform, real `NSEvent` through `[NSApp sendEvent:]`):

```
-- RIGHT ARROW as macOS sends it  (NSEvent modifierFlags=0xa00000 [NumericPad set])
  Qt received: key=0x1000014   modifiers=[KEYPAD ] -> the app's `mods == Qt::NoModifier` gate: DROPS THE KEY
-- KEYPAD 1 as macOS sends it  (NSEvent modifierFlags=0x200000 [NumericPad set])
  Qt received: key=0x31        modifiers=[KEYPAD ] -> the app's `mods == Qt::NoModifier` gate: DROPS THE KEY
-- MAIN-ROW 1 as macOS sends it (no flags)  (NSEvent modifierFlags=0x0)
  Qt received: key=0x31        modifiers=[NoModifier ] -> the app's `mods == Qt::NoModifier` gate: PASSES
```

**The one link I could NOT measure — stated plainly.** That macOS actually sets `NSEventModifierFlagNumericPad` on the four arrow keys is Apple's documented `NSEvent` behaviour, not something I reproduced: real HID injection was refused in this environment (`CGEventPostToPid` silently dropped; `osascript` → `System Events got an error: osascript is not allowed to send keystrokes. (1002)`; `AXIsProcessTrusted()==0`). So links 1–3 are measured; the arrow→flag link is documentation.

**The keypad half needs no such assumption and is fully VERIFIED**: a numeric‑keypad digit sets the numeric‑pad flag by definition, so `7` `Enter` typed on a full‑size keyboard's keypad does nothing. `src/present/key_translator.cpp:7-8` asserts the opposite in a comment — *"Only the main row is accepted; a keypad digit arrives as the same `Qt::Key_N`, so both work."* The `Qt::Key` is indeed the same; the modifier is not, and the comment is measurably false.

**Why 200+ tests are green:** `QTest::keyClick` never sets `KeypadModifier`, and every `onKey(...)` call in `tests/test_key_translator.cpp` passes `Qt::NoModifier` or an explicit chord — `grep -rn "Keypad" tests/` returns nothing. UAT‑3's human scenario 2 (*"Page through all 10 slides with →"*) is still `_pending_` in `docs/test-results/2026-08-05_uat-session-3-v1.md`, so no human has ever confirmed arrow navigation on the real Mac.

**Blast radius if confirmed:** `→ ← ↑ ↓` dead. `Space`, `PageDown`, `PageUp`, `Backspace` and `Esc` survive (not keypad/arrow keys), so there is a workaround — but the presenter will not know it mid‑talk, and a projector that ignores the arrow key reads as a frozen app. **Fix is one line** (mask `KeypadModifier` out before every comparison).

**30‑second check for the author, no permissions needed:** run `hb/keyprobe3.app/Contents/MacOS/keyprobe3` and physically press → and the keypad digits; it prints `keypad=` and whether the gate passes.

### F2 — SEV‑2 — The blackout escalates toward quitting, and the screen tells the audience otherwise

`hb/probe2` T2/T3, on a real 10‑slide deck through the real window:

```
      . from presenting, 1 x Esc  -> blackout
      . from presenting, 2 x Esc  -> QUIT PROMPT
      . from presenting, 3 x Esc  -> blackout
      . from presenting, 4 x Esc  -> QUIT PROMPT
...
      . blanked=1  'Right' -> RESUMED        . blanked=1  'Enter' -> still blank
      . blanked=1  'Space' -> RESUMED        . blanked=1  'P' -> still blank
      . blanked=1  'PageDown' -> RESUMED     . blanked=1  'Home' -> still blank
      . blanked=1  'Esc' -> *** QUIT PROMPT ***
      . 5 of 12 keys resume; the screen promises 'any key to resume'
```

The blackout displays `"Presentation paused — press Esc again to exit, any key to resume"` (`src/present/notice.cpp:36`). Only navigation leaves `Mode::Holding` (`presentation_controller.cpp:117-120`), so "any key" is false for 7 of the 12 keys I tried — and the key the line itself names, Esc, puts **"Quit the presentation?"** on the projector, where it sits for a measured **10080 ms** before auto‑dismissing (presenter S8). A presenter tapping Esc to get back shows the audience a quit dialog on every even press.

This sits inside deferred **BUG‑29**'s bucket ("the instruction is factually wrong"), so it is not new — but the measurement is, and 5/12 plus a 10‑second quit prompt on the projector argues for re‑triage above SEV‑3. Cheapest fix is text‑only: say which keys resume.

### F3 — SEV‑3 (latent, not reachable in v0.1) — a failed second `openDeck` puts the start screen over a live presentation

`hb/probe2` T6, deck live on slide 8, then a second `openDeck` of a non‑pptx:

```
      . after a FAILED second open: startVisible=1 presWindowVisible=1 deckStillPainted=1 strip='Slide 8'
  [FAIL] a failed open does NOT put the start screen up while a deck is still presenting
```

`AppShell::onDeckLoaded` (`src/ui/app_shell.cpp:136-142`) calls `showStart()` unconditionally on failure — correct on the launch path (BUG‑28), wrong once a window exists. **I verified it is NOT reachable today** (`hb/probe3` R1): Cmd+O aimed at the presentation window *and* at the hidden `StartView` both produce no dialog, because the shortcut's `Qt::WindowShortcut` context needs an active window and the start view is hidden. So this is a landmine for whoever wires "open another deck", not a live defect.

### F4 — SEV‑3 — Return/Enter does nothing on the new start screen

`src/ui/start_view.cpp:54-55` calls `setDefault(true)`/`setAutoDefault(true)`, but default‑button activation is a `QDialog` behaviour and `StartView` is a plain `QWidget`. Measured (`hb/probe3` R2, `hb/probe4` K3): Space on the focused button opens the dialog, Return and Enter do nothing. Cmd+O, the button and drag‑and‑drop all work, so it is a papercut — but the button draws a focus ring and then ignores the key everyone presses.

### F5 — SEV‑4 — A directory is reported as "That file could not be found."

`localDeckPathFrom` accepts a directory (it is a local URL) and the loader returns `FileNotFound`. Verified for both a plain folder and one named `*.pptx` (`hb/cliopen`, presenter S11). Harmless, but the message denies the existence of something the user is looking at.

---

## WHAT HELD — the failure modes the brief asked me to hunt, and did not find

- **No single keypress ends the talk.** 1470 key/modifier combinations swept at the presentation window (all ASCII printables, all of `Key_Escape`…`Key_Direction_R`, F1–F35, media/power/logoff keys × NoModifier/Shift/Cmd/Cmd+Shift/Alt/physical‑Ctrl/Keypad): zero closed the window, zero raised a dialog over the projector, zero quit the app, and the deck was still drivable afterwards (`hb/probe3` R3).
- **Nothing moves or reveals the deck while blanked.** Sweeping the blackout, exactly 8 combinations reveal it — `Space`, `Backspace`, and the four arrows plus PgUp/PgDn — all of them navigation, which is the designed behaviour. No leak (`hb/probe3` R4).
- **The quit prompt is escapable and cannot act on the deck.** 10 hammered keys left its pixels bit‑identical; Esc returns to the blackout; it auto‑dismisses at 10080 ms **to the blackout, never to the deck**; three consecutive window‑close requests refuse → blank → prompt → and the presenter gets back to presenting (presenter S6/S7/S8).
- **The app really terminates.** `hb/driver` calls the real `QApplication::exec()`: `esc,esc,ctrlq`, `esc,esc,ctrlshiftq` and an application `QEvent::Quit` all produce `lastWindowClosed` → `aboutToQuit` → `exec` returns, both on the CLI launch path and via the start screen (the hidden `StartView` does not keep the process alive).
- **Every bad input leaves a way forward.** Non‑pptx, .txt renamed .pptx, a directory, a directory named .pptx, and a missing file each show a dialog, leave the start screen up, leave no window on the projector, and a good deck still opens afterwards — 5/5 (presenter S11). The CLI failure path survives too (`hb/cliopen`).
- **Cmd+O and the Open button work** (`hb/probe2` T1 — BUG‑18 holds).
- **400 rapid Rights (305 ms) and 200 rapid Escs (301 ms)** leave the window intact, stopped at the last slide, and recoverable (`hb/probe2` T7).
- **All 8 fixtures touched by the last two PRs** — `good_bg_inherit`, `good_srcrect`, `good_srcrect_edge`, `good_srcrect_ub`, `good_stretch`, `good_pic_placeholder`, `good_image`, `good_text` — put a slide on the projector through the real widget stack (`hb/probe2` T4).
- Edges, rejection and the 1‑slide deck all behave: `End of deck — slide 47 of 47`, `Deck has 10 slides` for `99<Enter>` (rejected, never clamped), `Already at the first slide`.

## Coverage gap worth naming

`tests/ui/test_widgets.cpp:168` heads a block called *"the FIRST AppShell‑level tests"*, but `grep -rn "AppShell" tests/` shows **no test anywhere constructs `AppShell`** — those cases build a `PresentationController` and a `PresentationWindow` and drive the surface the way `AppShell` *would*. Every finding above except F1's translator half needed a real `AppShell` to surface. That, plus the absence of any `KeypadModifier` in the suite, is why F1 and F3 are invisible to 200+ green tests.

---

**TL;DR (plain English).** I built a fake presenter that opens made‑up slide decks and mashes the keyboard at the real app. The good news: I couldn't break it. No key ends the talk, nothing sneaks the deck back onto a blanked screen, you can always get out of the "really quit?" box, the app really does quit, and every bad file you throw at it still leaves you a way to try again. The bad news is one thing that could ruin the talk: on a Mac, the arrow keys secretly arrive carrying an extra "this came from the number pad" tag, and the app throws away any key that carries any tag at all — so the arrow keys may simply do nothing on stage. I proved every step of that chain except the last one (my sandbox wouldn't let me press a real key), so it needs a ten‑second hand check before the talk; the fix is one line. Space and Page Down still work as a backup. Two smaller things: the black privacy screen says "any key to resume" when only 5 of 12 keys do, and pressing Escape there puts a "Quit the presentation?" message in front of the audience for ten seconds; and the Enter key does nothing on the opening screen.