# powerpoint-voice — User Guide

A presentation controller that runs your PowerPoint deck and responds to five spoken commands.
Everything happens on your Mac: no internet, no accounts, nothing recorded.

---

## Before the talk

### 1. Install

Unzip `powerpoint-voice-test-build.zip` and put `powerpoint_voice.app` wherever you like.

**First launch: right-click the app → Open**, then confirm. A normal double-click will be blocked —
the app is not yet notarised by Apple. You only have to do this once.

If macOS still refuses:

```sh
xattr -dr com.apple.quarantine powerpoint_voice.app
```

**Requires macOS 26 or newer**, on Apple Silicon.

### 2. Allow the microphone

The first time you open a deck, macOS asks for microphone access. **Allow it** — that is what voice
control needs.

**If you decline, the app keeps working.** Voice turns off, a message says so, and the keyboard
drives everything. Nothing is lost except the voice commands.

### 3. Check it works — 60 seconds, worth doing

1. Open the app and load your deck.
2. Say **"next slide"**. The slide should change.
3. Read a sentence aloud that is *not* a command. The slide should **not** change.
4. Press the **right arrow** and **left arrow**. Both should work.

If step 2 fails, voice is not running — check the message on screen. If step 4 fails, stop and
investigate; the keyboard is the path that must always work.

---

## Running a presentation

### Opening a deck

Three ways, whichever suits:

- click **Open deck…**
- press **⌘O**
- **drag a `.pptx` onto the window**

The deck opens fullscreen. With a projector attached it goes to the **projector**, not your laptop
screen.

### The five voice commands

| Say this | It does |
|---|---|
| **"next slide"** | forward one |
| **"previous slide"** | back one |
| **"pause presentation"** | stops listening — use this for discussion |
| **"continue presentation"** | starts listening again |
| **"go to slide five"** | jumps to slide 5 |

Natural phrasing works: *"next slide, please"*, *"go to slide twenty three"*, *"resume the
presentation"*.

**Every command needs two words.** A bare "next" or "pause" does nothing — that is deliberate, so a
single word in conversation cannot move your deck.

### The keyboard — always available

| Key | Does |
|---|---|
| **→**, **↓**, **Space** or **Page Down** | next slide |
| **←**, **↑** or **Page Up** | previous slide |
| **type a number, then Enter** | go to that slide |
| **P** | pause voice — press again to resume |
| **Esc** | blank the projector (privacy) |
| **Esc** again | offer to quit |
| **⌘Q** | quit immediately |

The keyboard works whether or not voice is running. If anything at all goes wrong with the
microphone, keep presenting — nothing about a voice failure can stop the deck.

**P does exactly what "pause presentation" does**, so you never have to speak to silence the
microphone. **"Paused — voice control is off" appears on screen and stays there** until you resume,
so you can check at a glance before taking questions. Arrow keys keep working while paused — pausing
stops the *voice*, never the keyboard. With voice not running, P does nothing (there is nothing to
pause), and no message appears.

### With a screen reader

VoiceOver reads the presentation window. It announces which slide you are on ("Slide 3 of 10"),
whether the projector is blanked, and each notice as it appears. The window itself carries the key
list, so ⌃⌥ + the window gives you the controls without a menu bar.

It never reads the deck's own text. That is deliberate — the accessibility tree is readable by other
applications, and your slides are not something to publish there.

### During discussion

Say **"pause presentation"** — or press **P** — before you take questions. Voice stops responding
until you say **"continue presentation"** or press **P** again.

This matters: with a room talking, an isolated phrase that sounds like a command can occasionally be
misheard. Pausing removes that entirely. See *Known limits* below.

### Blanking the projector

Press **Esc** once. The projector goes black; your deck is not visible to the room. Press any
navigation key or say a command to come back.

Press **Esc** a second time and it offers to quit.

### Quitting

**⌘Q** quits immediately, from anywhere. Dock → Quit and Activity Monitor → Quit also work.

Closing the window with the red button does **not** quit mid-presentation — it asks first, so a stray
click cannot end your talk.

---

## Known limits

**Be aware of these before you present.**

### Isolated speech can occasionally trigger a command

The recogniser is restricted to the words in the five commands, but not to their exact order. A short
phrase spoken **alone**, with a pause either side, that sounds like a command — *"next flight"*,
*"applause presentation"* — can be misheard.

**Measured:** of 102 such near-miss phrases, 60 matched a command. Of six ordinary full sentences,
**none** did — the command must be the whole utterance, so anything mid-sentence is safe.

In practice: normal conversation and background chatter do not move your slides. Someone answering a
question with a two-word fragment occasionally might.

**If it happens:** say "previous slide", or press ←. And say **"pause presentation"** (or press
**P**) during discussion, which removes the risk entirely.

### Pictures the app cannot draw

Some PowerPoint images — EMF and WMF vector graphics — are not supported. They appear as a **grey box
with a cross**. Everything else on the slide is unaffected.

To fix a deck that has them, convert them first:

```sh
bash scripts/convert-deck-emf.sh yourdeck.pptx yourdeck-png.pptx
```

### Not supported

Tables, charts and SmartArt draw as a labelled placeholder box, not as content. Animations and
transitions are not run — slides change instantly.

---

## If something goes wrong

| What you see | What it means | What to do |
|---|---|---|
| A message saying voice is off | Microphone denied, or the speech model is missing | Keep presenting with the keyboard. Check System Settings → Privacy → Microphone |
| Slides do not respond to your voice | Voice is not running, or you are paused | Say "continue presentation"; check for a message on screen; use the keyboard |
| "Could not open the deck" | The file is not a readable `.pptx` | Try re-saving it from PowerPoint |
| A grey box with a cross | An EMF/WMF image | Expected — see above |
| The deck is on the wrong screen | Two displays, wrong guess | **Ctrl+Shift+D** moves it |
| The app will not quit | Should not happen | ⌘Q. Failing that, Activity Monitor → Force Quit |

**The keyboard is the fallback for everything.** If voice misbehaves in front of an audience, ignore
it and keep going with the arrow keys.

---

## What the app does with your data

- Your deck is **read only**, never modified, never copied, never uploaded.
- Speech is recognised **on your Mac**. No audio leaves it. Nothing is recorded.
- Nothing is written to disk while the app runs — no cache, no log, no transcript.
- No accounts, no telemetry, no network connections of any kind.

See `SECURITY.md` for the detail.
