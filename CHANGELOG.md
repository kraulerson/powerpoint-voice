# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/) with extended categories
for handoff clarity. Categories are ordered by impact severity.

<!--
  Category definitions:
  - Security: Vulnerability fixes, dependency patches for CVEs, auth changes
  - Data Model: Schema migrations, data format changes, rollback notes
  - Added: New features, new endpoints, new commands
  - Changed: Modifications to existing behavior
  - Fixed: Bug fixes (reference BUGS.md entry if applicable)
  - Removed: Removed features, deprecated endpoints
  - Infrastructure: CI/CD changes, dependency updates, configuration changes, tooling
  - Documentation: Significant doc updates (new ADRs, updated threat model, revised user guide)
-->

## [Unreleased]

### Security
- **BUG-81 — holding the P key left the microphone LIVE.** P toggles the voice gate and nothing
  checked for auto-repeat, so a held key (one press, ~5 repeats on macOS defaults) flipped it an even
  number of times and landed back on. The presenter believes voice is gated, turns to the room for
  questions, and the audience can drive the deck — **the exact failure the P key exists to prevent,
  re-created by the fix that made P work.** Auto-repeat is now swallowed for P and Esc, the two keys
  that are state machines rather than movements; arrows and digits still repeat.
- **BUG-83 — the real-time audio thread had no exception boundary.** The worker-thread boundaries
  below missed the one thread that runs *during* the whole talk. `onSamples` executes inside
  miniaudio's C data callback and allocates on every buffer — an exception unwinding out of a C-ABI
  frame is `std::terminate`, mid-sentence, with the deck on the projector. A dropped buffer is 20 ms
  nobody notices; nothing is logged, because that callback carries everything said in the room.
- **BUG-79 — the shutdown fix below rested on a guarantee the audio library does not provide.**
  `ma_device_stop()` does not join the callback on CoreAudio: the event it waits on is signalled on
  device **start** as well as stop and is latching, so the wait consumes a stale signal and returns
  immediately. The real barrier is the sink mutex — and it was taken *after* the device was
  uninitialised. It now runs first. The evidence chain is quoted, with line numbers, in
  `miniaudio_capture.cpp`.
- **BUG-75 (F-CHAOS-3) — an exception on either worker thread killed the process.** Both worker
  `start()` methods are slots invoked on a `QThread`, and Qt does not catch: an exception leaving one
  unwinds through that thread's event loop and `std::terminate()`s the app — no dialog, no fallback,
  the talk over. Both do work sized by an **untrusted file**, so `std::bad_alloc` is a normal outcome
  of a normal input. The loader now reports it as the ordinary "could not be opened" failure, with no
  exception text reaching a dialog that can land on the projector (TM-013). The renderer's boundary
  is **per slide**, so one unrenderable slide costs one placeholder box rather than the other
  ninety-nine.

### Fixed
- **BUG-72 (F-1) — quitting freed the speech engine underneath the live audio thread.**
  `~AppShell` stopped the deck and render workers and never stopped voice, so `VoskEngine` was
  destroyed while CoreAudio's real-time thread was inside `feed()`. Invisible on the development
  machine, which has no microphone; live on the presenter's laptop, on every Cmd+Q at the end of a
  talk. Voice is now torn down first — the pipeline stops (which joins the audio thread), then the
  engine, then the gate — and the member declaration order enforces the same thing independently.
  Measured: **7/7 heap-use-after-free in the old order, 0/7 in the new.** Two things about this fix
  were wrong when first written and are corrected above: the mechanism it named (BUG-79) and its
  regression test, which pinned a property of a test double and survived deleting the entire fix.
- **BUG-73 (F-2) — the P key did nothing.** `PresentationWindow::setPaused()` had zero callers, so
  the key always translated to "pause presentation", which the presentation controller treats as a
  no-op. A presenter pressing P before taking questions would have believed voice was gated while it
  was still fully live. The keyboard now reaches the same gate the voice does, in both directions.
- **BUG-74 (F-CHAOS-2) — an abandoned render worker painted the previous deck.** A worker whose
  shutdown wait expires is deliberately abandoned rather than terminated (BUG-42), and it keeps
  rendering: its slides landed in the raster set after it had been resized for the **new** deck.
  Every index was in range, so nothing complained. Now guarded twice — the connection is cut at
  teardown, and a deck generation counter drops anything already queued.
- **BUG-77/78 (A11Y-1/A11Y-2) — the presentation was silent to VoiceOver, and the pause gate was
  voice-only.** Every surface is custom-painted, so a screen reader saw one unnamed rectangle: the
  deck, the privacy blackout, the quit prompt and a hung app were indistinguishable, and with no menu
  bar there was nothing to discover the key map from. The window now carries the key map, the surface
  says which slide is showing, and a notice is announced rather than left to be polled. **The state
  is named and the deck's content never is** — the accessibility tree is readable by other processes
  (TM-012/013). A11Y-2 is closed by the P-key fix above.
- **BUG-82 — a pause that worked looked exactly like a pause that did nothing.** Pausing showed no
  message at all (resuming did), so the presenter had no way to confirm the microphone was gated
  before taking questions. The "Paused — voice control is off" notice already existed in the code and
  was unreachable: nothing emitted it, and the one caller that could display it passed a hardcoded
  "not paused".
- **BUG-87 — that fix then did not compile on Linux.** The announcement API is Qt 6.8+; this project
  develops and ships on macOS with Qt 6.11, and CI builds against Ubuntu's older packaged Qt. Now one
  shared version-guarded helper (`src/ui/a11y_announce.*`), which below 6.8 is a deliberate no-op —
  the alternatives there are events the macOS bridge discards, and raising one would restore the "we
  announce" claim while changing nothing anyone can hear, which is precisely what BUG-80 was.
- **BUG-80 — the screen-reader announcements were a no-op on macOS.** They used two Qt event types
  that Apple's side of Qt discards, so VoiceOver said nothing; the tests asserted the stored text,
  which was set correctly either way. Now uses the one event type that reaches the platform.
- **BUG-84 — the test-registration lint had three holes**, all found with a deliberately broken probe
  project: it failed **open** if no file matched `*tests`, it was blind to a newline in a test name
  (the same class of bug it was written for), and it wrongly blocked the build on any ordinary
  non-doctest check. It now discovers binaries from ctest's own command lines and compares **counts**,
  which no separator can disguise.
- **BUG-76 — tests that had never once run, while the suite reported 100% green.** Counted precisely:
  **five** had been dead since Phase 2 and UAT remediation, a **sixth** was committed dead the same
  day (the C-02 regression test below), and a **seventh** was caught in the working tree before it
  could be committed. A TEST_CASE name
  containing `;` is split by CMake's list separator, so ctest invokes a fragment that matches no test
  case: doctest runs 0 cases, exits 0, and ctest prints "Passed". One of the seven was the C-02
  regression test committed the same day. `scripts/lint-test-names.sh` now compares the ctest
  registration against the binaries' own list and fails on any mismatch (see BUG-84 for what that
  check had to become before it was trustworthy).
- **C-01 — a picture on slide 1 of the reference deck vanished again, and my own BUG-58 fix is what
  did it.** That fix namespaced a placeholder key by the non-visual-properties element containing
  it. The deck's slide-1 picture sits under `<p:nvPicPr>` while the layout entry that positions it
  sits under `<p:sp>`, so the two stopped matching, the picture inherited no geometry, went back to
  `0x0` and was silently never drawn — BUG-41 reopened by its own remedy, with no warning emitted.
  The commit claimed "not triggered on Karl's deck (all 45 layout placeholders sit under nvSpPr)";
  that checked the layout side only, and a key has to match on **both**. Placeholder identity is
  `type:idx` again, which is what ECMA-376 defines and what PowerPoint writes. The key collision
  BUG-58 was actually about is handled where it belongs — at insertion, first entry wins — so
  document order (which is z-order, not priority) no longer decides whose geometry a slide adopts.
- **C-02 — naturally-phrased commands stopped working.** Vosk emits a literal `[unk]` token wherever
  it heard something outside the grammar, so "okay, next slide" decodes as `[unk] next slide`.
  Measurement across a 60-clip corpus of natural phrasings: **36 of 60 rejected**. An `[unk]` at one
  edge is now stripped. At **both** edges it is not: `"the next slide shows our results for this
  quarter"` decodes as `[unk] next slide [unk]`, so stripping both would turn narration into a slide
  change and reopen TM-002/019.

### Security
- **BUG-21 — the TM-018 render caps measured the wrong quantity.** Shapes and text runs alone let
  through slides that take minutes: 2000 pictures of one 31 Mpx image measured ~309 s, and 5000 runs
  x 300k characters ~657 s, both under the two original caps. ISOLATE held — the UI never blocked —
  but the slide never appeared, which during a talk is the same as losing it. Total characters and
  total DECLARED image pixels now complete the ratified four-cap set (Bible section 3, TM-018.3-A).
  The reference deck sits three orders of magnitude under every cap.
- **BUG-22 — the raster cache was unbounded.** 3840x2160 RGB32 is ~31.6 MB per slide, so a 300-slide
  deck is ~9.27 GB: the machine swaps, then the OOM killer takes the app mid-talk. A fixed 2 GB
  window now evicts the slides FURTHEST from the one being shown, and never the one being shown —
  evicting that would blank the projector, the outcome the whole subsystem exists to prevent.

### Changed
- MVP cutline amended: **F5 and F6 moved below the line as a deliberate scope cut** (Karl,
  2026-08-09). The keyboard itself is built and tested — what F6 additionally specified was a
  keybinding configuration surface — and Karl performs the pre-show check himself. Recorded in
  PRODUCT_MANIFESTO.md section 5 rather than left as a silently unmet gate condition.


### Added
- **F8d — voice decode. The application now listens and acts.** Microphone → 16 kHz mono →
  grammar-constrained Vosk → the recognizer gate → the presentation controller. The decoder is
  created with `vosk_recognizer_new_grm` against a dynamic-graph model, which constrains the
  **vocabulary** — measured across 404 utterances, every word emitted was in the grammar. It does
  **not** constrain word ORDER: Vosk compiles the grammar into a backoff bigram, not a phrase
  matcher. An earlier version of this entry claimed the decoder was "incapable of emitting anything
  but the five commands"; that was false and is withdrawn (BUG-66).

### Security
- **BUG-65 closed.** Vosk silently drops grammar words the model does not know, quietly widening
  what can be recognised. Every grammar word is now verified present in the model's vocabulary
  before the recogniser is created; any unknown word leaves voice off.
- **Vosk's stderr logging is silenced before any model is loaded** — it prints decoder internals
  including heard words, which on this project is a disclosure channel (TM-012/013).
- Recognised text is never stored, logged or rendered. It passes from the decoder into the gate and
  nowhere else; only closed-vocabulary notice ids reach an operator or audience surface.


### Security
- **F8c — voice will not run unless the grammar can actually be enforced.** The safety property is
  that the decoder is *incapable* of producing anything but the five commands, not that it prefers
  them: the audience is within earshot of the microphone (TM-002/TM-019). Vosk fails **open** here —
  a model built with a static graph accepts a grammar and then decodes the full ~200,000-word
  vocabulary with no error at all. Such a model is now refused and voice stays off. The vendored
  model is asserted grammar-capable by a test, so a future model swap that loses the dynamic graph
  fails loudly rather than silently widening what can be heard.
- The grammar JSON is **built character by character from an allow-list** rather than interpolated:
  malformed grammar JSON segfaults Vosk rather than returning an error, so nothing may depend on the
  input being well formed.

### Added
- Tests asserting the grammar and the command matcher cannot drift apart: every grammar phrase must
  be one `matchCommand()` accepts, and no word in the grammar may lie outside the five commands.


### Added
- **F8b — microphone capture.** Captures from the system default input device through miniaudio →
  CoreAudio and converts whatever the device provides into the recogniser's 16 kHz mono. Bound to the
  API rather than to hardware: default device only, never an index or name, and the format is asked
  for rather than requested — a requested rate would make CoreAudio resample silently and leave us
  unable to tell what we were actually given. Every capture failure is recoverable by construction,
  because the keyboard is the guaranteed control path and no microphone problem may end a talk.
  Not yet wired to a recogniser; that is F8c.
- **The vendored Vosk library is now loadable (BUG-49) and the model is extracted into the build
  (BUG-50).** The shipped library's install name is a bare leaf, so dyld never consults `@rpath` and
  it failed at process start; a prepared copy is fixed up at build time, leaving `third_party/`
  byte-identical and SHA-256-verifiable. The 39 MB model is unzipped at build time rather than first
  run, because TM-011 forbids writing to disk during a talk.

### Fixed
- **BUG-56 — the audio API could not express buffer length**, so a device changing sample rate or
  channel count mid-stream produced a heap over-read. Confirmed under AddressSanitizer.


### Fixed
- **BUG-47 — a picture saved in ISO 29500 Strict format became completely invisible.** `amt="50%"`
  and `l="25%"` are the Strict spellings, written by PowerPoint's own "Strict Open XML Presentation"
  save option. `QString::toInt()` returns **0** for them without reporting failure, and 0 means fully
  transparent for opacity and no-crop for the source rectangle. This was introduced by the BUG-37/38
  fix itself — a new way to silently lose a picture, in the same change set whose sibling commit
  exists because a picture was being silently lost. Both spellings now parse, failures are reported
  rather than swallowed, and the fallbacks are chosen so a parse failure can never hide anything: an
  unreadable opacity draws **opaque**, an unreadable crop shows the **whole** picture, each with a
  warning.
- **BUG-48 — an over-crop drew a mirrored region the deck explicitly excluded.** With `l+r > 100000`
  the computed width is negative, and `QRectF::intersected` *normalises* it — swapping the edges and
  returning a valid rectangle over exactly the excluded region. The downstream "cropped to nothing"
  guard never fired, because the normalised width is positive. Insets that meet or cross are now
  rejected before any rectangle is built.

### Changed
- Test fixtures for source cropping now cover top/bottom insets, asymmetric left/right, both
  percentage spellings, over-crop, and unparseable garbage. **Mutation-tested**: six mutations that
  previously survived the whole suite — dropping the `t` parse, dropping the `b` parse, swapping `l`
  and `r`, reverting to `toInt()`, removing the over-crop guard, and making an unreadable opacity
  transparent — now each turn the suite red.


### Security
- **The app would have been killed by macOS the first time voice was armed (BUG-45).** `MACOSX_BUNDLE ON`
  makes CMake generate a default `Info.plist` with no `NSMicrophoneUsageDescription`, and macOS
  *terminates* a process — not an error return, not a catchable dialog — when it opens an audio input
  device without one. It would have failed **only on the presenter's MacBook Pro**: the development
  Mac mini has no microphone to trigger it. `cmake/MacOSXBundleInfo.plist.in` now supplies the key,
  with a consent string that states plainly that recognition is on-device and nothing is transmitted.

### Added
- **`src/audio/audio_format.*` — capture-format conversion as pure functions** (BUG-46). The talk runs
  on a MacBook Pro M3 Max whose built-in microphone is a three-element array that CoreAudio typically
  presents at 48 kHz; a headset or AirPods can be 44.1 kHz and/or stereo. Vosk needs 16 kHz mono, and
  feeding it 48 kHz does not error — it decodes audio at three times the intended speed as plausible
  words, the worst failure mode for a command recogniser. The device now reports its format and we
  downmix and resample. Multi-channel input is **averaged, never sampled on channel 0**: the array
  elements are not equivalent, and taking one discards most of the beam-formed signal.
  Nine tests, including a sine-shape assertion — a length-only check would pass for silence.


### Fixed
- **BUG-43 — the quit prompt advertised the macOS system "Log Out" shortcut.** My own BUG-35 fix
  changed the hint to "Cmd+Shift+Q", which is ⇧⌘Q — the system Log Out shortcut. Printing that on a
  projector invites the presenter to log the machine out mid-talk. The macOS hint is now **"Cmd+Q"**,
  which the translator already accepts and which, per Karl's BUG-36 ruling, quits from any mode.
- **BUG-41 — a picture placeholder was silently dropped; slide 1's main photograph never rendered.**
  PowerPoint writes a picture placed into a layout's picture placeholder as a `<p:pic>` carrying
  `<p:ph type="pic" idx="11"/>` and **no `<p:spPr>`** — the layout positions it. `placeholderKey()`
  looked for `<p:ph>` only under `<p:nvSpPr>`, which a `<p:pic>` does not have, so it matched no
  layout entry, kept a 0x0 rect, and the renderer skipped it. No warning was produced: the picture
  was simply absent. `<p:ph>` is now looked for under `nvSpPr`, `nvPicPr`, `nvGraphicFramePr` and
  `nvCxnSpPr`, layouts index their `<p:pic>` placeholders too, and a picture with no geometry of its
  own adopts the layout's — the same inheritance already used for text (BUG-2) and backgrounds (BUG-32).
- **BUG-37 — `<a:srcRect>` source cropping was ignored, so pictures were drawn whole.** Karl reported
  a slide-1 graphic sitting in a white box on the dark navy background. That picture is a JPEG, and
  JPEG cannot store transparency — the white is in the pixels. The deck's answer is to **crop it
  away**: `l="29178" r="29178"`, 29.178% off each side. We drew all of it. Slide 9's picture is
  cropped 31.6%/40.9% and was equally wrong. `<a:srcRect>` is now read from the `<p:blipFill>` (a
  direct child, so a nested fill elsewhere in the `<p:pic>` cannot supply the wrong crop) and passed
  as the source rectangle; negative insets, which ask for padding outside the image, clamp to the
  image bounds instead of producing an out-of-bounds source rect.
- **BUG-38 — `<a:alphaModFix>` picture opacity was ignored.** Slide 1's two EMF graphics are declared
  at 70% opacity and rendered fully opaque. Opacity is now applied per draw and restored immediately,
  so one translucent picture cannot wash out everything drawn after it.


### Fixed
- **BUG-31 — the application could not be quit by any graceful means. Second attempt; the first fix
  was insufficient.** Not the window button, not Cmd+Q, not Dock → Quit, not even Activity Monitor's
  Quit — only Force Quit (SIGKILL). Qt documents the mechanism for `QCoreApplication::quit()`: the
  request "may be ignored if the application prevents the quit, for example if one of its windows
  can't be closed." An application quit is delivered by asking every top-level window to close, and
  Qt cancels the shutdown if any window refuses. `closeEvent` refused every close request from every
  source, so the app was structurally unquittable. The previous fix raised the quit prompt on a close
  request but still `ignore()`d it, so it changed the symptom and not the defect.
  The two requests are now told apart: an application-level `QEvent::Quit` — the interception point
  Qt documents — closes the windows with an "application is quitting" flag raised, so the window
  accepts from any mode with no prompt; an ordinary window close is still refused and still raises the
  deliberate two-step prompt. The flag is scoped to that single call, so it never leaks into a later
  window.
  **Reproduced and verified with the real instrument**: `NSRunningApplication::terminate()`, which is
  exactly what Activity Monitor's Quit button calls, against a real `PresentationWindow` — refused
  before the fix, terminates gracefully after it.
- **BUG-35 — the quit prompt named a chord that cannot be typed on macOS.** It said "Ctrl+Shift+Q",
  but Qt maps the Command key to `Qt::ControlModifier` on macOS and the physical Control key to
  `Qt::MetaModifier`, so a Mac user following the hint presses a chord that arrives as `Meta|Shift`
  and matches nothing. The hint is now platform-correct ("Cmd+Shift+Q" on macOS), and a test asserts
  the hint names the chord the translator actually accepts.

### Added
- `src/ui/quit_policy.{hpp,cpp}` — the single place that decides who may end a presentation.
- **GROUP Q** tests: an application quit is obeyed from every mode including the privacy blackout; an
  ordinary window close is still refused; the quit flag does not leak to a later window; the on-screen
  hint names a chord this platform actually delivers. Three of the four fail with the fix disabled.

### Changed
- The existing test "a close request is REFUSED unless quitting was confirmed" was **asserting the
  bug** — it pinned refusal for close requests from every source, which is what made the app
  unquittable. Narrowed to user-initiated closes and cross-referenced to GROUP Q.


### Fixed
- **BUG-32 — every slide background rendered white.** The loader read `<p:bg>` only at slide level,
  but real decks almost never put it there: Karl's carries a background on **zero of its 10 slides**,
  on **12 of its 17 layouts** and on **its master**. Backgrounds now resolve up the OOXML chain —
  slide → layout → master — the same inheritance already implemented for font size (BUG-8) and
  placeholder geometry (BUG-2). `<p:bgRef>` into the theme's `bgFillStyleLst` resolves too when the
  referenced entry is a plain solid fill. Verified on the real deck: all 10 slides now resolve
  (`373F51 / 0076A3 / EAF0F6 / FFFFFF ×4 / 0076A3 / EAF0F6 / 0098D1`), matching its layout and master
  structure exactly, with zero warnings. Four of those are dark, so this should also fix text that
  was previously white-on-white. Layouts and masters are parsed once each and cached rather than
  re-read per slide.
- **A background we cannot paint now warns instead of rendering silently white.** A gradient, picture
  or pattern fill previously fell through to `None` with no signal at all. The first part that
  declares a background ends the inheritance walk even when unpaintable — falling through would paint
  a colour the deck does not specify (Manifesto F1: never a silent wrong render). The warning names
  the fill KIND from a fixed vocabulary and carries no deck content (TM-012/013).
- **BUG-30 — the app segfaulted on launch on the real deck. Root cause corrected; the previous
  diagnosis was wrong.** The macOS crash report
  (`powerpoint_voice-2026-08-04-190008.ips`) names it: the faulting thread is the pre-render
  `QThread` inside `QFontDatabasePrivate::findFont` → `initFontDef` → `QString::operator=`, while the
  main thread is inside `QApplicationPrivate::handleThemeChanged()` — a data race on Qt's global
  font/theme state, not the image path the earlier fix addressed. `AppShell` started the render
  worker and *then* created and showed the fullscreen window, and showing a widget window is what
  fires that handler. Three GUI-thread mitigations, all before any worker exists: the application
  font is now set **explicitly** (Qt skips the theme-change font reset when the application claims
  it); `QFontDatabase::families()` is populated up front instead of lazily from the worker; and the
  render worker starts only **after** the window is shown, via a queued call. Residual risk from
  spontaneous theme changes mid-pre-render is tracked as **BUG-34**.

### Added
- **Tests that run the REAL renderer on a REAL worker thread** (`GROUP RT`). Every previous
  pre-render test injected a fake render function, so the production `SlideRenderer` had never been
  executed off the GUI thread by any test — which is precisely where BUG-30 lived, behind 183 green
  tests. Four cases now load a fixture deck and pre-render it through the production renderer on a
  `QThread`, including EMF and GIF payloads and the inherited-background deck.
- Fixtures `good_bg_inherit.pptx` (background on the layout and on the master), `good_bg_unsupported.pptx`
  (gradient, picture and `bgRef` fills) and `good_emf_image.pptx`.

### Infrastructure
- The theme fixture now carries a `bgFillStyleLst`, so `<p:bgRef>` resolution is exercised.


### Security
- **F7b hardened after an adversarial audit** (all findings reproduced in real builds; ThreadSanitizer
  clean). Five Critical, all fixed: a use-after-free that made the app **SEGV on the first arrow key
  after pre-render** (Qt destroys workers on `QThread::finished`; raw pointers dangled → now
  `QPointer`); a 0-slide deck producing an **unquittable fullscreen black projector**; the deck's
  **full file path rendered into a dialog** that lands on the projector (Bible §8 / TM-013); an
  attacker-triggered use-after-free in the loader (`st.name` read after `zip_close`) that **printed
  freed heap into that dialog**; and media-part read amplification with a **~640 GB allocation
  ceiling** from a ≤200 MB file. Plus the privacy blackout never actually blanking, an invisible
  quit prompt that never timed out, and unbounded shutdown. Findings:
  `docs/security-audits/f7b-usable-presenter-security-audit.md`.
- **F7a presentation funnel hardened** after adversarial audit: `undoJump()` was a second
  index-computing entry point that moved the deck behind the quit overlay and the privacy blackout;
  the blackout could be dismissed by a *rejected* command and by a voice "continue presentation"
  while paused (an audience-Q&A disclosure path, TM-002/012/019); the quit prompt self-destructed on
  a real wall clock and its timer was signed-overflow UB. All fixed test-first and verified under
  ASan+UBSan. Findings: `docs/security-audits/f7a-presentation-funnel-security-audit.md`.
- **Command grammar requires an object (BUG-17):** a bare "pause"/"continue"/"resume" is no longer
  a command. Accepting single words gave the audience a ONE-WORD un-pause during Q&A — the exact
  window the Paused state exists to protect (TM-002/019) — and the filler strip reduced ordinary
  speech ("okay lets continue", "and now continue", "lets pause") to that lone word. Now
  "pause/continue/resume (the) presentation" is required; 7 regression assertions lock it. Found by
  the voice-engine design review; the residual stuck-in-Paused risk is covered by keyboard parity
  (F6), resequenced ahead of the voice engine.
- Voice-command layer (F2/F3) hardened at the recognizer boundary: the dispatch sink is
  exception-guarded so a throwing command handler can never cross the audio-thread boundary
  and crash the app mid-talk; a reentrancy backstop prevents double-dispatch/recursion; the
  `IRecognizer` contract pins finalized-phrases-only + same-thread delivery (prevents one
  utterance firing multiple jumps and a `state_` data race). Grammar matching is phrase-level
  (audience speech cannot false-trigger a command) and logs no heard text (Bible §8). Findings:
  `docs/security-audits/f2-f3-voice-commands-security-audit.md` (2-agent adversarial audit).
- Deck loader (F1a) hardened against hostile .pptx input: ZIP64 size-cap wrap bypass closed
  (unsigned comparison), XML descendant walk made iterative to prevent stack-overflow DoS,
  per-slide shape cap added to prevent parse-time memory exhaustion. Findings + remediation:
  `docs/security-audits/f1a-deck-loader-security-audit.md` (5-agent audit).
- Renderer (F1b) hardened: font-size clamp (prevents a giant-glyph hang), image decode
  allow-listed to PNG/JPEG with dimension + allocation caps (excludes CVE-prone TIFF/WebP/GIF
  codecs), clip rect (no letterbox bleed), and per-text-box paragraph/run/text caps. Findings:
  `docs/security-audits/f1b-slide-renderer-security-audit.md` (2-agent audit).

### Data Model
- Added the in-memory slide model (`src/model/slide_model.hpp`): Presentation → Slide →
  ShapeElement (TextBox | Image | Unsupported) / Background / LoadWarning. `ImageElement` now
  carries raw image bytes; `UnsupportedElement` carries geometry for placeholder rendering.
  No persisted schema (standalone app).

### Added
- **Feature F7b — Usable Presenter**: the app now actually presents. `File`/CLI opens a `.pptx`
  **off the UI thread**, every slide is **pre-rendered off-thread** before the talk (TM-018), and the
  deck is shown fullscreen on the external display with keyboard navigation, a privacy blackout
  (Esc), and a deliberate two-step quit. New modules under `src/present/` (display geometry, key
  translation, deck-load worker, pre-render worker) and `src/ui/` (slide surface, notice strip,
  presentation window, app shell). A second test binary (`pptv_ui_tests`) tests the widget layer.
- **Feature F7a — Presentation Funnel**: `PresentationController`, the single place in the product
  that computes a slide index, so both input paths (voice, keyboard) are range-checked once and it
  cannot be bypassed. Rejects out-of-range slide numbers rather than clamping (BUG-16); a
  quit-confirm state machine in which quitting is unreachable from any command; a privacy blackout
  (holding screen); and a CLOSED notice vocabulary (an id plus two ints, never free text) so deck
  content has no channel to a display or a log. See `src/present/`.
- **Feature F2/F3 — Voice-Command Grammar & Dispatch**: `matchCommand()` maps a recognized
  phrase to one of the five closed-grammar commands (next/previous slide, pause/continue
  presentation, go to slide N) or nothing — phrase-level matching so audience speech never
  false-triggers. `RecognizerController` dispatches commands through an Active/Paused listening
  gate: while paused, navigation is ignored (Q&A protection) and only "continue presentation"
  resumes. Pure and unit-tested; the speech engine plugs in behind the `IRecognizer` interface
  (a follow-on feature). See `docs/api and interfaces/voice-commands.md`.
- **Feature F4 — Slide-Number Parser**: `parseSlideNumber()` turns "go to slide N" text
  (digits, number words, digit-by-digit) into an integer; fails safe (nullopt) on garbage,
  malformed sequences, or overflow so a mis-heard command never jumps to the wrong slide.
- **Feature F1a — Deck Loader**: `DeckLoader::load()` parses an untrusted .pptx (libzip +
  pugixml) into the slide model — text runs with font/size/weight/color, EMU positions, solid
  backgrounds, resolved image references + bytes, and warnings for unsupported elements. Resource
  caps enforced (file size, slide count, per-part/cumulative decompression, per-slide shapes,
  per-box paragraphs/runs/text).
- **Feature F1b — Slide Renderer**: `SlideRenderer::render()` paints a slide to a QImage —
  backgrounds, positioned text with font/size/weight/color, images, letterboxing, and visible
  placeholders for unsupported elements and missing/disallowed images. Pure and deterministic.

### Changed
### Fixed
- A missing/unresolvable slide no longer silently drops (which drifted "go to slide N" onto the
  wrong slide); a placeholder slide preserves numbering and a warning is surfaced (audit F1a-3).

### Removed
### Infrastructure
- CI + local build gain libzip and pugixml (system packages; `.github/ci-deps-apt.txt`).

### Documentation
- ADR-0001 (Qt6 + Vosk architecture), Phase 1 threat model / data model / UI scaffold,
  Project Bible (16 §), and the F1a security-audit findings.
