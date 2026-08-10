# Pre-talk check — MacBook Pro M3 Max, 2026-08-10

**Build:** `~/Desktop/powerpoint-voice-test-build.zip` (80 MB, arm64, macOS 26+)
**Time needed:** about 10 minutes.
**Deck:** your real one. The point of this run is the machine you will present from.

Copy the zip across, unzip, then **right-click `powerpoint_voice.app` → Open** and confirm.
That first launch step is once per machine — the app is ad-hoc signed, not notarised.

---

## The five checks, in priority order

Test 1 is the one I most want your eyes on. It is a defect I introduced and an
adversarial reviewer caught: it would have left your microphone live during Q&A.

### 1. The P key — pausing the microphone ⚠️ HIGHEST PRIORITY

| Do | Expect |
|---|---|
| Open your deck. Allow the microphone when macOS asks. | Deck fullscreen on the projector or laptop screen |
| **Tap P once** | **"Paused — voice control is off" appears and STAYS on screen** |
| Say "next slide" | **Nothing happens** — the deck does not move |
| Press → | Deck advances. The keyboard is never gated |
| **Tap P again** | "Resumed" appears; the paused message is gone |
| Say "next slide" | Deck advances |
| **Now HOLD P down for a full 2 seconds**, then release | **Ends up PAUSED and stays paused.** The message is on screen |

**That last row is the whole point.** Before this build, holding P flipped the gate once
per key-repeat and landed back on *live* — you would have believed the microphone was
off while it was still listening. If holding P leaves it unpaused, stop and tell me.

### 2. Voice — the five commands still work

The entire audio layer changed in this build (crash guards, shutdown ordering), so this
is a regression check, not a new feature.

Say each one and confirm the slide responds:

- "next slide" · "previous slide" · "go to slide three" · "pause presentation" · "continue presentation"

### 3. Natural phrasing — this was broken and is now fixed

Say these as you would in the room, with the filler words:

- **"okay, next slide"**
- **"next slide please"**
- **"let's go to slide five"**

All three should work. In the last build, 36 out of 60 phrasings like these were rejected
because the recogniser tags the surrounding speech and my filter threw the whole thing away.

Then say this **as a full sentence** and confirm the deck does **NOT** move:

- **"the next slide shows our results for this quarter"**

### 4. Slide 1's photograph

Look at slide 1. The photograph on the right-hand side should be there, full height.

It disappeared again in the last build — one of my own earlier fixes broke it — and it is
back. Also confirm the rest of the deck looks the way it did last time.

### 5. Quitting after voice has been running

| Do | Expect |
|---|---|
| With the deck open and voice armed, press **⌘Q** | Quits immediately and cleanly |
| Reopen, use voice for a minute, then Dock → Quit | Quits cleanly |

This is the path that had a use-after-free: the app was freeing the speech engine while the
microphone thread was still inside it. It only ever fired on a machine with a microphone —
never on the Mac mini — so this build is the first time it can be checked at all.

---

## If something is wrong

Tell me which numbered step and what you saw. The keyboard is the guaranteed control path:
arrows, space, Page Up/Down, type-a-number-then-Enter, Esc to blank, ⌘Q to quit. **Nothing
about a voice failure can stop you presenting.**

## Known and accepted, do not re-report

- A **grey box with a cross** on slide 1 — the two EMF graphics we cannot decode. Use the
  `-png` converted deck if you would rather not see them.
- An isolated near-miss phrase can still trigger a command (measured: 60 of 102 fragments;
  **0 of 6 natural sentences**). Pausing during discussion removes it entirely — which is
  what check 1 is protecting.
