# Threat Model & Architecture Stress Test — powerpoint-voice

<!--
  Phase 1 Step 1.3 output. Persona: hostile Penetration Tester (fresh eyes, no
  inherited assumptions). Target: ADR-0001 (Qt 6.8 + Vosk 0.3.45 + from-scratch
  OOXML renderer). Threat IDs TM-NNN are permanent and are the traceability key
  for Phase 2 security audits (Step 2.4) and Phase 3 validation (Step 3.2,
  templates/generated/threat-model-validation.tmpl).

  Source of authority: ADR-0001; docs/phase-0/data-contract.md;
  PRODUCT_MANIFESTO.md §4 (data contracts) and §8 (Q1–Q13 resolutions).
-->

**Date:** 2026-08-03
**Phase:** 1, Step 1.3
**Persona:** Penetration Tester (adversarial review of the selected architecture)
**Architecture under test:** ADR-0001 — Qt 6.8 LTS (Core/Gui/Widgets, LGPL dynamic) +
QPainter/QTextLayout/QRawFont/QImage renderer + Vosk 0.3.45 grammar-constrained
recognizer + miniaudio capture + libzip 1.11 + pugixml 1.15 + spdlog 1.14, CMake/Ninja,
macOS-first DMG via macdeployqt.
**Status:** Draft for Phase 1→2 gate

---

## 0. Scoping statement — what is NOT in this model

**There is no network attacker.** The product has zero runtime network I/O by hard
constraint (Intake §6.4; Manifesto §7; data-contract "Third-Party Integrations: None").
No server, no API, no auth, no accounts, no sessions, no tokens, no TLS, no CORS, no
CSRF, no SSRF, no injection into a query language, no rate limiting, no horizontal
privilege escalation between tenants — **none of these apply and none are padded into
this document.** The classic web threat model is empty here by construction, and that is
the single largest security benefit of the architecture.

What remains is a *file-parsing and acoustic* threat model. Everything hostile arrives
through one of four doors:

| Door | Trust level | Why it is hostile |
|---|---|---|
| The `.pptx` file | **Fully attacker-controlled** | Decks arrive by email and USB. Karl opens what he is sent. This is the primary attack surface and gets the most attention below. |
| The room's air (microphone) | **Fully attacker-controlled during the talk** | An always-on recognizer in a room containing an audience is an unauthenticated command channel with no speaker identity. |
| Local files the app reads (settings JSON, recent-files list, bundled Vosk model, Qt plugins/dylibs) | **Attacker-controlled after any local compromise, and pre-compromise if the app bundle is user-writable** | These are the pivot and persistence surfaces. |
| The dependency supply chain (Qt, Vosk, libzip, pugixml, miniaudio, spdlog, aqtinstall, FetchContent) | **Attacker-controlled if any upstream or CI is compromised** | Build-time compromise defeats every runtime control in this document. |

## 0.1 Assets, ranked by what the attacker gets

| Rank | Asset | Why it ranks here |
|---|---|---|
| A1 | **Availability of the running presentation during the live executive talk** | Explicitly the worst outcome. A crash, hang, or blank screen in front of an executive audience is unrecoverable in the moment and is the failure the whole product exists to avoid. Uniquely, availability outranks confidentiality here. |
| A2 | **Integrity of what is displayed** | A silently wrong slide (wrong number, wrong content, wrong slide order) is worse than a visible error, because Karl will speak to it as if it were correct. |
| A3 | **The Confidential deck content** | Executive material. Never leaves the machine, never to disk, never to logs, never to crash dumps. |
| A4 | **Live room audio** | Confidential; zero retention by contract. A compromise that turns an always-on mic into a recorder is a bugging device in an executive meeting. |
| A5 | **Karl's machine** | The deck is the delivery vehicle; code execution here is the attacker's actual goal in the "malicious deck" scenario. |

## 0.2 Threat actors

| Actor | Capability | Motivation |
|---|---|---|
| **TA-1 — Deck author (targeted)** | Crafts a `.pptx` and gets it to Karl by email/USB, knowing he will open it, knowing roughly when the talk is. Cannot touch the machine otherwise. | Code execution on Karl's machine, or a timed denial-of-service during a specific named meeting. |
| **TA-2 — Hostile audience member** | Can make noise. Can speak. Can play audio from a phone. Sits 3–10 m from the mic. No physical access to the laptop. | Disrupt or embarrass the presenter; force navigation to material not meant to be shown; make the tool look unusable. |
| **TA-3 — Local attacker with file write** | Has, or has obtained (see TM-021), write access to `~/Library/Application Support/…`, the recent-files list, or the app bundle. | Persistence, deck exfiltration by file staging, audio capture. |
| **TA-4 — Supply-chain attacker** | Compromises an upstream release, a FetchContent-fetched tag, the Vosk model download, or the CI runner. | Ship the backdoor inside the signed-looking product; defeats every runtime control. |
| **TA-5 — Shoulder-surfing observer** | Reads the projector and the laptop screen. | Opportunistic disclosure of anything the app renders. |

---

## 1. STRIDE — concrete attack paths

Each threat states the attack as an attacker performs it, then a mitigation that is a
buildable technical control. Manifesto/data-contract controls already decided are cited
so Phase 2 knows what is a *new* obligation versus an existing one.

### S — Spoofing

#### TM-001 — Audience voice injection: any human in the room is an authenticated operator
**Attack.** I sit in row 3 during Q&A. The recognizer is always on and there is no speaker
identification anywhere in this stack — `vosk-model-small-en-us-0.15` carries no speaker
model, and even if it did, nothing in ADR-0001 enrolls Karl's voice. I say, at conversational
volume, **"go to slide 47."** The grammar contains that phrase, so Vosk matches it and the
shared action layer dispatches it — the dispatcher cannot distinguish my voice from Karl's,
because by the time it sees a command the audio is gone. Karl is now on a backup slide he
did not intend to show. Escalation: I do it during the pause between his sentences so it
reads as a system malfunction, and I repeat it every time he recovers.

**Amplifier specific to this stack.** A grammar-constrained Vosk recognizer is a *nearest-match*
device: if the grammar JSON passed to `vosk_recognizer_new_grm()` does not include the
`"[unk]"` token, every acoustic event in the room is forced onto its closest in-grammar
phrase. The closed grammar — the control chosen to *reduce* false triggers — becomes a
false-accept amplifier if that one token is missing. A cough can become "next slide."

**Mitigation (concrete).**
1. `"[unk]"` **must** be a member of the grammar array passed to `vosk_recognizer_new_grm()`.
   This is a hard build requirement, not a tuning knob; add a unit test that feeds
   babble/pink-noise fixtures and asserts the result is `[unk]`, and a test that asserts
   the literal `[unk]` is present in the compiled grammar string.
2. Confidence gate on the *word* level from Vosk's result JSON (`conf` per word in
   `SetWords(true)` output), not just the phrase level; below threshold → overlay shows
   heard text, dispatch nothing (already the data-contract T6 behavior).
3. Two-word grammar (already decided, Manifesto Q1) raises the acoustic bar versus bare
   "next"; keep it and never widen the grammar (Manifesto §7 forbids growth).
4. **Pause discipline as an enforced state, not a habit:** "pause presentation" before Q&A
   (Manifesto Q1/Q5) — while paused, only "continue presentation" executes. Phase 2 must
   implement pause as a hard gate in the *dispatcher*, so a matched "go to slide N" while
   paused cannot reach slide state even if the recognizer matches it.
5. Post-MVP escape hatch already recorded: push-to-talk (Manifesto §6), justified by any
   real false-trigger incident.
6. **Directional mic guidance in the user guide** — a headset/lapel mic at ~15 cm gives
   a ~20 dB advantage over a row-3 talker; this is a deployment control that costs nothing.

#### TM-002 — Recorded/masked audio playback under acoustic cover
**Attack.** Rather than speak, I play a pre-recorded "previous slide" from a phone speaker
held low, timed to land under applause, a laugh, or the HVAC cycle. Recorded speech is
cleaner than my live voice at that distance, so it matches *better*. Nothing in the stack
does replay detection, liveness detection, or channel analysis. Repeat every 30 seconds:
the deck walks backwards and the presenter looks incompetent.

**Mitigation (concrete).**
1. **Command rate limiter in the action layer:** at most N navigation actions per rolling
   window (suggest 3 per 5 s), and a minimum inter-command interval (suggest 700 ms).
   Excess commands are shown in the overlay as "ignored (too fast)" and not dispatched.
   This converts a sustained playback attack into a visible, bounded annoyance.
2. **Direction-of-travel sanity:** a "previous slide" immediately following a "next slide"
   inside 1.5 s is the signature of injection or of a double-trigger; require confirmation
   (second utterance) or suppress with an overlay notice.
3. Overlay always shows *what was heard and what was done* (already F5) — the presenter
   sees the injection happen and can invoke pause discipline; this is the human control
   that makes the technical ones usable.

#### TM-003 — Trojaned build spoofing the product
**Attack.** The DMG is unsigned and un-notarized (Platform Module §3.3 permits this for
MVP). I mail Karl `powerpoint-voice-1.0.1.dmg` "with the rehearsal fixes," or I leave it
on a USB stick. He right-clicks → Open to bypass Gatekeeper (he has been doing exactly
that for his own unsigned builds all week, so the gesture is habitual and unremarkable).
My build is byte-identical in behavior plus a mic recorder.

**Mitigation (concrete).**
1. **Ad-hoc signing at minimum from day one**, Developer ID + notarization before the app
   leaves Karl's machine for any second user. Cost: $99/yr, already in the Platform Module.
2. **Publish SHA-256 of every artifact in the GitHub Release body**, and make the release
   step a CI job so the hash is produced by the pipeline, not by a human.
3. **In-app build provenance:** the About/holding screen shows the short commit hash and
   build date compiled in via CMake (`GIT_COMMIT` compile definition). Karl can verify in
   two seconds that the binary running in the room is the one CI built.

### T — Tampering

#### TM-004 — Zip-slip and symlink escape through OOXML part names
**Attack.** OOXML part names are just zip entry names, and libzip returns them verbatim. I
craft entries named `../../../../Users/karl/Library/LaunchAgents/com.evil.plist` and
`ppt/media/../../../../.zshenv`. If any code path writes a part to disk — a media temp
file, a font temp file, a "extract for debugging" branch, a crash-artifact dump — I get an
arbitrary-file write as Karl's user, which is game over (LaunchAgent = persistence,
`.zshenv` = code execution on next terminal). Variant: a zip entry that is a **symlink**
(zip stores unix mode bits in the external attributes) pointing at
`~/Library/Application Support/powerpoint-voice/`, followed by an entry that writes
"through" it.

**Mitigation (concrete).**
1. **The architecture's real defense is that nothing is ever extracted to disk** —
   data-contract G-6 and Manifesto §4 State ("deck content is never written to disk by
   the application; no thumbnail caches, no temp extraction"). Phase 2 must make this
   *mechanically true*: all part access goes through one `OoxmlPackage::readPart()`
   returning a `QByteArray`, and a CI grep/Semgrep rule forbids `QTemporaryFile`,
   `QFile::open(WriteOnly)`, `fopen(..,"w")`, and `zip_file_get_contents`-to-disk anywhere
   in the OOXML subtree.
2. **Part-name canonicalization gate** regardless, because defense in depth costs 20 lines:
   reject any entry name containing `..` as a path segment, a leading `/`, a backslash, a
   NUL, a drive letter, or a non-ASCII/percent-encoded traversal; reject names longer than
   260 bytes; require the name to match `^[A-Za-z0-9_\-./]+$` and to resolve under a known
   OOXML prefix (`ppt/`, `docProps/`, `_rels/`, `[Content_Types].xml`).
3. **Reject any entry whose external attributes indicate a symlink or a non-regular file**
   (`zip_stat` + `zip_file_get_external_attributes()`, check `S_ISLNK`/`S_ISDIR` in the
   high 16 bits for the unix-made-by case). Fail the load with a named part, per §4.1-1.

#### TM-005 — Settings-file tampering to disarm the keyboard fallback
**Attack.** The settings JSON is a plain local file with no integrity protection (correctly
— there are no credentials to protect and OS file permissions are the stated control). I
get one write (phishing script, a shared machine, a "helpful" config someone sent). The
keybindings map is the interesting field: **the keyboard path is the designed fallback for
every voice failure in this product.** I remap every command to an unreachable key, or set
overlay fade duration to `0` (overlay never visible → Karl cannot see what the recognizer
is doing), or point the mic device at a nonexistent identifier. Now voice control fails at
the talk *and* the fallback fails with it, and Karl has no diagnostic surface.

**Mitigation (concrete).**
1. **Strict schema validation with a rejecting parser**, already required (data-contract
   Input 3, G-3): unknown keys rejected by name, values range-checked, malformed file →
   defaults + visible warning, never overwrite the bad file without user action.
2. **Invariant that cannot be configured away:** the keybinding validator must reject any
   map that leaves any of the five MVP commands unbound or that binds two commands to the
   same key — this is already stated in the data contract ("keybindings must not unbind a
   Must-Have command") and must be a unit-tested function, not a comment.
3. **Hard-coded default bindings always remain live** in addition to user bindings (arrows,
   space, digits+Enter, Esc) — user config may *add*, never *remove*. This makes TM-005
   structurally unexploitable for the availability goal.
4. Range-clamp rather than accept: fade duration clamped to [1 s, 10 s]; out-of-range →
   clamp + named warning at load.
5. Settings are re-validated on every change, not only at startup, so a file swapped while
   the app runs cannot take effect unvalidated.

#### TM-006 — Bundled Vosk model / grammar tampering
**Attack.** The model is ~40 MB of read-only files inside the app bundle. If the bundle
lives in `~/Applications` or on the Desktop (very likely for an unsigned dev build), it is
user-writable, so a script with Karl's privileges can rewrite it. Two payloads: (a) corrupt
the model so `vosk_model_new()` returns NULL or crashes — a targeted denial of the entire
voice feature; (b) subtler, replace the grammar resource so the recognizer accepts a much
wider vocabulary, re-opening the false-trigger surface that Manifesto Q1 was written to
close, silently, with no user-visible change.

**Mitigation (concrete).**
1. **Startup integrity verification is already mandated** (data-contract Input 4:
   "presence and integrity verified at startup"). Make it a real check: SHA-256 of every
   model file compared against a manifest of hashes **compiled into the binary** (a
   generated `model_manifest.h` produced at build time), not a hash file sitting next to
   the model where the attacker can update both.
2. **The grammar is source code, not data** — the five phrases + number vocabulary are a
   `constexpr` string literal in the binary, never a loadable resource file. There is
   nothing on disk to swap.
3. Failure → the already-designed "mic unavailable" state: persistent banner + keyboard
   fallback (§4.1-2). Never fail open into an unverified model.
4. Hardened runtime + library validation (see TM-022) makes the bundle tamper-evident at
   the OS level once the app is signed.

#### TM-007 — OOXML part confusion: ambiguous zip and duplicate part names
**Attack.** A zip archive has two sources of truth: the central directory and the local
file headers. I build an archive where the central directory and the local headers
disagree, or where **two entries share the name `ppt/slides/slide3.xml`**. libzip indexes
by the central directory and `zip_name_locate()` returns one of them; a different tool (or
a different code path in the same app, e.g. an integrity pre-scan that iterates indices
0..n versus a render path that looks up by name) sees the other. Result: the deck Karl
reviews at home renders slide 3 benignly and the deck the app renders in the room shows
different content — an **integrity** attack (A2), silent and invisible, which is worse
than a crash because he will speak to it.

**Mitigation (concrete).**
1. **Single-pass canonical index.** Enumerate entries exactly once by index
   (`zip_get_num_entries` + `zip_get_name(i)`), build one immutable `name → index` map, and
   **reject the package if any name appears twice** (case-insensitively, and after Unicode
   NFC normalization — `SLIDE3.XML` vs `slide3.xml` matters on macOS's case-insensitive
   filesystem). Every subsequent read uses the index, never a name lookup.
2. **Reject archives with trailing data after the end-of-central-directory record** and
   archives with a non-zero "start of archive" offset (concatenated-zip / polyglot files).
3. Enforce the OPC rules the format already gives us: `[Content_Types].xml` must exist and
   must be entry-name-unique; every part read must be reachable from `ppt/presentation.xml`
   through `_rels` relationships — **unreferenced parts are never parsed at all**, which
   deletes a large slice of attack surface for free.

### R — Repudiation

Repudiation is unusual here: there is one user and no accounts, so "user denies an action"
is not the risk. The real repudiation risk is **the app cannot attribute a slide change to
a source**, which matters precisely because TM-001/TM-002 exist.

#### TM-008 — Unattributable slide changes after an incident
**Attack.** I execute TM-001 successfully during the talk. Afterwards, the question is
"did the tool malfunction, or did somebody in the room drive it?" The debug log is **off by
default** (Manifesto Q7), the in-memory session log is lost when the app closes, and heard
text is only in an opt-in rehearsal log that is also off. There is no record. The tool
takes the blame for my attack, which is my second-best outcome — and Karl has no evidence
to justify enabling push-to-talk or changing venues.

**Mitigation (concrete).**
1. **The in-memory session command log is always on** and records, per dispatch:
   monotonic timestamp, session correlation ID (spdlog, from ADR-0001), **`source` field
   (`voice` | `keyboard`)**, matched command ID, confidence score, and end-to-end latency.
   This is Manifesto-legal today: Q7 permits command events / confidence / timings; only
   *heard text* is restricted.
2. **A user-invoked "export session log" action** writes that in-memory buffer to a
   user-chosen path on demand. On-demand export is not "logging deck content" and does not
   violate Q7 or the data contract — the record contains command IDs and numbers only.
3. **Never log deck content, ever** (data contract, binding rule) — the log records
   `GOTO_SLIDE target=47`, never the slide's text.
4. Overlay history for the session is retained in memory so the last N events can be
   reviewed on the holding screen immediately after the talk, before the process exits.

#### TM-009 — No record of *which* deck was rendered
**Attack.** I swap the file at the path in the recent-files list between Karl's rehearsal
and the talk (TM-003-adjacent, requires local write). He double-clicks the same recent
entry. The app has no notion of deck identity, so nothing anywhere records that the bytes
changed. "The slide was wrong" has no forensic answer.

**Mitigation (concrete).** At load, compute and log (never display in the room) the
SHA-256 of the `.pptx` and its size + mtime; show the short hash on the **pre-show check
screen only** (Manifesto Q4's pre-show voice check is already a natural home for it). Karl
compares it to the rehearsal hash in two seconds. The hash of a file is not deck content.

### I — Information Disclosure

#### TM-010 — XML entity expansion / external entity file disclosure
**Attack.** The reflex attack on any XML pipeline: `<!DOCTYPE t [<!ENTITY xxe SYSTEM
"file:///Users/karl/.ssh/id_ed25519">]>` inside `slide1.xml`, with `&xxe;` in a text run,
so the private key is rendered on the projector; or a billion-laughs entity cascade to
exhaust memory.

**Assessment against pugixml 1.15 specifically — and this is the point of doing the model
against the real library.** pugixml does **not** implement DTD processing: it does not
declare, resolve, or expand custom entities, and it never fetches external resources of any
kind. Under `parse_default` the doctype is skipped entirely; even `parse_doctype` only
*retains the doctype node as text* without processing it. Only the five predefined XML
entities are expanded. **So XXE and billion-laughs are structurally not exploitable here** —
not because we validate, but because the parser has no such feature. This is a genuine
security advantage of the pugixml choice and should be recorded as such.

**The risk is therefore that this property is silently lost.** Mitigation is an invariant,
not a filter:
1. **A locked-in negative test** in the Phase 2 suite: a fixture deck containing an XXE
   payload and a billion-laughs payload; asserts the entity is *not* expanded, no file is
   read, and memory stays bounded. This test fails loudly if anyone swaps pugixml for
   libxml2/expat/QXmlStreamReader-with-DTD, which is the actual failure mode.
2. **Parser pinning is architectural:** pugixml 1.15 is pinned by FetchContent (ADR-0001);
   an ADR is required to change the XML parser, and the ADR checklist must reference TM-010.
3. Parse with explicit flags — `pugi::parse_minimal | parse_escapes | parse_cdata` — never
   `parse_full` (which adds doctype/PI retention we have no use for), and never
   `parse_ws_pcdata_single` sloppiness that would change text semantics.
4. `load_buffer` from an in-memory `QByteArray` only — never `load_file()`, so there is no
   path-taking XML entry point at all.

#### TM-011 — Confidential deck and room audio exfiltrated through crash artifacts
**Attack.** I don't need code execution; I need the app to *crash* while holding the deck.
I ship a malformed font (TM-016) that reliably segfaults the font engine. On macOS,
`ReportCrash` writes an `.ips` report into `~/Library/Logs/DiagnosticReports/` containing
thread state and, depending on configuration, memory regions; if core dumps are enabled at
all (`ulimit -c`), a full core lands in `/cores` containing the entire parsed slide model,
decoded images, embedded fonts, **and the audio ring buffer**. I then retrieve it later
(TA-3), or Karl helpfully attaches it to a bug report. Manifesto Q8 forbids this outcome;
the default OS behavior produces it.

**Mitigation (concrete).**
1. `setrlimit(RLIMIT_CORE, {0,0})` as the **first statement in `main()`**, before Qt is
   initialized — Manifesto Q8's "no default core dumps" made mechanical. Unit-testable via
   a child process that raises SIGSEGV and asserts no core file appears.
2. **Arena-allocate all Confidential data** (slide model, decoded images, font blobs, PCM
   ring buffer) from a small number of named regions, so a fatal-signal handler can
   `explicit_bzero()` those regions in O(regions) before re-raising. Zeroize-then-die is
   the only way to keep a crash report from containing the deck.
3. **Audio buffers zeroized immediately after each recognition pass**, not merely dropped —
   the data contract says "discarded"; make it overwrite, not free.
4. **Never place deck-derived strings into diagnostic text:** `qFatal`, `Q_ASSERT` messages,
   exception `what()`, and every spdlog call take element *types and part names* only
   (`"unsupported element a:graphicFrame in ppt/slides/slide7.xml"`), never element content.
   Enforce with a Semgrep rule that flags any log/assert call taking a variable of the
   slide-model text types.
5. Disable Qt's crash-handling extras and do not link any crash-reporter SDK — trivially
   satisfied, since network upload is forbidden anyway.

#### TM-012 — The transcript overlay discloses room audio to the room
**Attack.** This one is free — I don't have to do anything. The overlay renders heard text
on **the presenting display, which is the projector**. Manifesto Q5 resolves that while
paused the app "displays heard text, executes nothing." So during Q&A, with pause engaged,
whatever the recognizer thinks it hears — an executive's half-audible aside, a name, a
number — is projected in a text strip at 4.5:1 contrast in front of the whole room. TA-5
just reads the wall. This is a designed feature disclosing Confidential-derived data (the
data contract already classifies overlay heard-text as Confidential-derived).

**Mitigation (concrete).**
1. **Split the overlay by display role.** On the **audience-facing** display, render only:
   the matched command token ("NEXT SLIDE"), the state glyph, the slide counter, and
   "no match" — a fixed, closed vocabulary that can never contain room speech. Render
   **full heard text only on the operator (laptop) display**. This preserves Q4's
   listening-visibility and Q5's "show what was heard" intent, on the surface only Karl
   sees. In single-display mode, heard text is shown (there is no alternative) — document
   that in the user guide as a known posture.
2. Cap the rendered heard-text length (e.g. 48 chars) and never let it exceed the reserved
   strip (already §4.1-5) — long stretches of overheard conversation cannot scroll across
   the slide.
3. Heard text auto-fades in ~3 s (already Q6) — keep it in the transient class, never the
   persistent class.

#### TM-013 — Deck subject leaked by filenames on the holding screen and in window titles
**Attack.** `Project-Ares-Redundancies-Board-Only-v9.pptx`. The recent-files list is
rendered on the app's dark holding screen (Q3), which is *on the projector* between decks
and at end of show, and the macOS window title/Dock/Mission Control show the filename. I
photograph it from row 3. No parsing required. The data contract already flags this: deck
paths are Internal precisely because "file paths/names may hint at confidential subject."

**Mitigation (concrete).**
1. **The holding screen shown on the audience display never lists recent files or the
   current filename** — it shows the app name and nothing else. The recent-files UI exists
   only in the pre-show/operator surface.
2. Window title is a constant string in presentation mode; the deck name appears only in
   the pre-show window on the laptop.
3. Recent-files entries display the basename only, bounded to 10 entries (data-contract
   G-9), with a one-click "clear recent files."

### D — Denial of Service

A1 says availability is the top asset. This is the section that matters most.

#### TM-014 — Decompression bomb through libzip, including compression-method confusion
**Attack.** DEFLATE gives ~1032:1, so a 4 MB entry expands to ~4.1 GB. I put that in
`ppt/media/image1.png`'s slot, or in a slide XML part. Naive code calls `zip_stat()`,
reads `st.size`, and allocates — but `st.size` comes from the archive metadata, which is
**mine**, so I can also lie in the other direction (declare 1 MB, deliver 4 GB) to defeat a
size check that trusts the header. Second lever: libzip supports compression methods beyond
store/deflate (bzip2, xz, zstd where built in). **zstd with a crafted dictionary reaches
ratios far beyond DEFLATE's 1032:1**, so a bomb that passes a "ratio ≤ 1032" heuristic is
easy to build if non-OOXML methods are accepted. Result: OOM kill, or minutes of swapping,
during load — and if Karl loads the deck two minutes before the talk, that *is* the attack.

**Mitigation (concrete).** The Manifesto already decided the caps (Q9: ≤1 GB total
decompressed + per-part caps); this is how they must be implemented:
1. **Never trust `zip_stat`.** All reads go through a bounded `zip_fread()` loop into a
   caller-supplied buffer with a hard byte budget; when the budget is exceeded, abort the
   read and fail the load naming the part. Never call `zip_file_get_contents()`.
2. **Three independent budgets, all enforced during streaming:** per-part cap (suggest
   50 MB for XML parts, 100 MB for media parts), **total decompressed budget 1 GB** across
   the whole package (Q9), and an **entry-count cap** (suggest 5,000 entries) checked
   before any read, since 100,000 tiny entries is its own bomb.
3. **Compression-method allowlist: `ZIP_CM_STORE` (0) and `ZIP_CM_DEFLATE` (8) only.**
   Anything else fails the load. This is what the OOXML/OPC specification permits anyway,
   so it costs zero compatibility and removes the zstd/xz/bzip2 amplification classes
   outright.
4. **Ratio circuit-breaker per entry:** if decompressed bytes exceed `comp_size × 200` for
   a single entry, abort that part. Deliberately below DEFLATE's theoretical max so
   pathological-but-legal compression still fails closed.
5. Existing outer bounds stay: file ≤200 MB, slides ≤300 (data contract Input 1).
6. **Every rejection names the failing part and the exceeded limit, and the app keeps
   running** (§4.1-1) — a rejected deck must never be a crashed app.

#### TM-015 — Image pixel bomb via QImage
**Attack.** A 40 KB PNG whose IHDR declares 65,535 × 65,535 px. Decoded at 4 bytes/px that
is **17 GB**. A plain `QImage img(bytes)` or `QImage::loadFromData()` attempts the
allocation. On macOS the process is OOM-killed or the machine begins swapping hard. Cost to
me: 40 KB inside a deck that otherwise passes every zip cap in TM-014, because the bomb is
in the *dimensions*, not the compressed size. Variants: a 100,000-frame animated GIF; a
tiled TIFF with absurd strip counts; a WebP crafted for the libwebp huffman-table overflow
class (CVE-2023-4863) — Qt's imageformats plugins wrap libtiff/libwebp/giflib, which are
where the image CVEs actually live.

**Mitigation (concrete).**
1. **Use `QImageReader`, never `QImage::loadFromData()`.** Call `reader.size()` (header
   only, no decode), reject if `width × height` exceeds a pixel budget (suggest 64 Mpx) or
   either dimension exceeds 20,000, **before** calling `read()`.
2. **Set `QImageReader::setAllocationLimit()` explicitly** to a value we choose (suggest
   128 MiB) rather than relying on Qt 6's 256 MiB default, and set it per-reader.
3. **Restrict the enabled image formats to PNG and JPEG** by pinning
   `QCoreApplication::setLibraryPaths()` to the bundle and shipping only `qjpeg`/`qgif`-free
   plugin sets — i.e. remove the `qtiff`, `qwebp`, `qgif`, `qicns`, `qtga` plugins from the
   bundle at `macdeployqt` time. Any other format → visible placeholder (which the renderer
   already must draw for unsupported elements, §4.1-1). This removes libtiff/libwebp/giflib
   from the attack surface entirely for a format tier the MVP does not promise.
4. **Call `QImageReader::setAutoTransform(false)`** and ignore EXIF orientation; EXIF
   parsers are a recurring bug source and orientation is irrelevant for deck media.
5. Decode budget is *cumulative*: track total decoded bytes across the deck against a
   ceiling (suggest 1.5 GB) — 300 slides × a legal 5 Mpx image is a bomb built from
   entirely legal parts (see B1 in §2).

#### TM-016 — Malformed embedded font: crash or memory corruption in the system font engine
**Attack.** The best target in the whole package. `QRawFont::loadFromData()` on macOS hands
my bytes to **CoreText** (`CTFontManagerCreateFontDescriptorFromData`) — a large,
memory-unsafe, closed-source system framework running **in my target's process**. I craft
an OpenType file with a malformed `CFF ` charstring, an out-of-range `loca` offset, a
cyclic `glyf` composite chain, or a `cmap` format-14 subtable with bogus counts. Best case
for me: heap corruption → code execution (see TM-021). Very good case: a deterministic
segfault → TM-011's crash artifacts. Baseline case: the font engine takes 30 s hinting a
pathological glyph and the app hangs mid-talk. This surface exists *because* ADR-0001 chose
QRawFont for embedded fonts — it is the price of the decision and must be paid explicitly.

**Mitigation (concrete).**
1. **Pre-validate every embedded font before it reaches QRawFont**, with an
   OpenType-sanitising pass: verify the sfnt header, table count ≤ 64, every table's
   offset+length inside the blob, no overlapping tables, required tables present
   (`head`, `hhea`, `maxp`, `cmap`, `name`, and one of `glyf`+`loca` / `CFF `), font blob
   size ≤ 8 MB, `numGlyphs` ≤ 65,535 and consistent with `loca` length. Rejecting on any
   check is cheap and kills the overwhelming majority of malformed-font payloads.
2. **Handle the OOXML `fntdata` obfuscation explicitly** — PowerPoint XOR-obfuscates the
   first 32 bytes of embedded font parts with the GUID from the part name. Deobfuscate
   *before* validation. (Also an edge case: see E1 in §2.)
3. **Fail into the substitution path, which already exists and is already visible**
   (data-contract T3, Intake §11-3: every substitution must be surfaced at load, never
   silent). A rejected font is a warning line, not a crash.
4. **Hard timeout on font load and on first glyph shaping** — run deck-load font
   resolution on a worker thread with a deadline (suggest 2 s/font); on timeout, abandon
   and substitute. This covers the "slow, not crashing" variant that a validator cannot
   detect statically.
5. Phase 3 obligation: **fuzz the font pre-validator** (libFuzzer/AFL++ over the OOXML
   font-part parser) — this is the highest-value fuzz target in the codebase.

#### TM-017 — XML amplification and recursive-descent stack exhaustion
**Attack.** Two distinct shapes, neither needing entity expansion (see TM-010):
(a) **DOM amplification** — a 400 MB decompressed `slide1.xml` (passing TM-014's per-part
cap if it is set generously) of ~10 M tiny nodes; pugixml's DOM carries per-node overhead
on top of the document buffer, so peak RSS runs to several GB. (b) **Depth bomb** — 200,000
nested `<a:grpSp>` elements. pugixml's own parser is non-recursive and survives this;
**our from-scratch slide-model walker will be a recursive descent over the DOM and will
blow the 8 MB main-thread stack** — a clean, immediate SIGSEGV with no chance to recover.
The vulnerability is in *our* code, invited by the DOM design.

**Mitigation (concrete).**
1. **Explicit depth counter in the walker, capped at 100** nested shape/group levels; over
   the cap → unsupported-element placeholder + load warning, exactly like any other
   unsupported construct. Cheap, total, and testable with a generated fixture.
2. **Per-part XML cap well below the total budget** — suggest 50 MB decompressed for any
   `.xml` part (real slide XML is kilobytes; 50 MB is already absurd). Enforced by TM-014's
   streaming budget before pugixml ever sees the bytes.
3. **Node-count ceiling:** after `load_buffer`, walk once and abort past ~2 M nodes; or
   simpler and equivalent, derive it from the byte cap.
4. **Parse and build the slide model on a worker thread with a larger, explicit stack**
   (`QThread::setStackSize`) so even a mis-set depth cap degrades to a thread failure the
   UI can report, not a process death.
5. `pugi::xml_document` destroyed as soon as the slide model is built — never held for the
   life of the presentation.

#### TM-018 — The mid-deck render bomb: the highest-severity live-presentation threat
**Attack.** This is my best attack against A1, and it is designed to defeat Karl's
rehearsal. Slides 1–12 are ordinary and render in 30 ms. **Slide 13** contains 200,000
one-character text runs with 40 distinct fonts, or a 12,000-vertex custom-geometry path
with per-vertex gradients, or a 40 Mpx image scaled into a 200 px box with smooth
transformation. All of it is *legal OOXML* — no malformed anything, no bomb, no cap
exceeded, so every input validation in this document passes. QPainter/QTextLayout takes
45 seconds on that one slide, **on the UI thread**, so the app is a beach-balling frozen
window on a projector in front of an executive audience. If Karl's pre-show check is
"open the deck, confirm slide 1 looks right" — which is exactly what a pre-show check is —
he sees nothing wrong. The payload detonates at the moment of maximum cost, and it looks
like the tool is simply broken.

**Mitigation (concrete).**
1. **Validate and pre-render *every* slide at load time, never lazily.** The load-time
   warning report already exists (Output 4) as the natural place to surface "slide 13
   exceeded the render budget." Load is the only safe place to discover this, because at
   load Karl can still choose a different deck.
2. **Per-slide complexity caps enforced during model build, before painting:** total text
   runs ≤ 5,000/slide; total shapes ≤ 2,000/slide; path vertices ≤ 10,000/shape; distinct
   fonts ≤ 32/deck. Over any cap → the slide renders as a visible placeholder with the
   reason, and the deck still presents.
3. **Render off the UI thread with a hard deadline** (suggest 750 ms/slide at load,
   200 ms during the show per §5.2): a `QImage` painted on a worker, deadline-checked via
   an abort flag polled by the paint loop; on expiry, that slide becomes a placeholder.
   **The UI thread must never be able to block on deck content.** This single control is
   the difference between "one slide is a placeholder" and "the presentation is over."
4. **Pre-render the whole deck to rasters at load into a bounded memory budget** (LRU with
   neighbour prefetch, see B1) so that during the show, slide change is a blit and cannot
   invoke the parser or the font engine at all. Slide-change latency becomes independent of
   deck content — which is what the <200 ms contract (§5.2) actually requires under
   adversarial input.
5. **Pre-show check must be all-slides, not slide-1** — extend the Q4 pre-show check screen
   to report "40 slides rendered, 0 placeholders, slowest 120 ms." Two seconds of Karl's
   attention converts this threat into a visible warning.

#### TM-019 — Audio pipeline wedge kills voice control mid-talk
**Attack.** Less crafted, more opportunistic, and it will also happen by accident. The
miniaudio capture callback runs on a real-time audio thread; Vosk's `AcceptWaveform` is a
synchronous, compute-heavy call. If Vosk is invoked from the audio callback, one slow
inference stalls the callback, CoreAudio drops the device, and the recognizer silently
stops — no crash, no error, just a mic that has stopped working. TA-2's assist: unplug the
podium USB hub / knock the lapel mic dongle, and CoreAudio's default-device change wedges
a naive capture loop. The banner never appears because the app does not know.

**Mitigation (concrete).**
1. **Three threads, one lock-free ring buffer:** audio callback writes PCM only (no
   allocation, no locks, no logging); a recognition thread drains the ring and calls Vosk;
   the UI thread receives matched commands by queued connection. The audio callback must
   never call into Vosk, Qt, or spdlog.
2. **Watchdog on frame arrival:** if no audio frames arrive for >2 s while not paused,
   raise the persistent "mic unavailable" banner (§4.1-2) and attempt device re-open with
   exponential backoff. Silence must be *detected*, not assumed to be quiet.
3. **Handle `ma_device_notification_type_stopped`/device-change** by tearing down and
   re-initialising against the current system default device (data-contract G-8), keeping
   the banner up until frames resume.
4. **Vosk engine restart on crash with an overlay alert** is already required (§4.1-3);
   bound it (suggest 3 restarts / 60 s) then degrade permanently to keyboard with the
   persistent banner, rather than restart-looping through the talk.
5. **The keyboard path shares nothing with the audio subsystem** — already the design
   (data-contract Input 5: "fully independent of the speech engine"); Phase 2 must be able
   to demonstrate it by killing the recognition thread and driving the whole deck by key.

#### TM-020 — Acoustic denial of the recognizer, and hostile "pause presentation"
**Attack.** Two low-effort variants for TA-2. (a) **Noise flood:** continuous babble,
a phone playing a crowd track, or a chair scraped rhythmically raises the noise floor so
Vosk never reaches confidence; voice control appears dead for the whole talk. (b) **Wedge
by protocol:** I say **"pause presentation."** Karl says "continue presentation." I say
"pause presentation" again. Because pause is designed to be idempotent and to suppress
everything except "continue," and because I can speak whenever I like, I can hold the
system in a state where his own voice commands do nothing — using only the safety control
itself, with no exploit at all.

**Mitigation (concrete).**
1. **Keyboard parity is the answer to both** (F6) — it is unaffected by the room, and this
   is precisely the scenario it was designed for. The user guide must state plainly: if the
   room is fighting you, use the keyboard and stop arguing with the microphone.
2. **State-change rate limiting** on pause/continue specifically (suggest: at most one
   pause↔continue transition per 3 s), which makes the (b) wedge visibly ineffective.
3. **Pause state is only *exitable* by keyboard as well as voice** — pressing any mapped
   key resumes (already the data-contract dispatch rule: "dispatch while paused: only
   'continue' — or any keyboard key — acts"). Keep that rule; it defeats (b) outright.
4. **Persistent PAUSED indicator** (Q6) so the state is never ambiguous to the presenter.
5. Noise-floor telemetry on the pre-show check (Q4): report measured SNR in the venue
   before the talk, so a hostile or simply bad room is discovered at 09:00, not at 09:31.

### E — Elevation of Privilege

There are no roles, no accounts, and no privilege boundary *inside* the app. "Elevation"
here means: **content crosses the boundary from parsed data into executed code**, gaining
the privileges of Karl's user account.

#### TM-021 — Memory-safety exploitation in a deck parser → code execution as Karl
**Attack.** Four independent memory-unsafe C/C++ parsers process my file **in the same
process** as the presentation UI and the live microphone stream, with no sandbox, no
process isolation, and no seatbelt profile: libzip (my archive structure), pugixml (my
XML), Qt's imageformats plugins wrapping libjpeg-turbo/libpng/libtiff/libwebp (my pixels),
and CoreText/FreeType via QRawFont (my fonts). I need one heap overflow in any of them.
The font path (TM-016) is the softest, and image codecs have the richest public CVE history.
Once I have execution, I am Karl's user: I can read every file he can, and I am already
inside a process that holds the deck in memory and has a live microphone handle and a
granted TCC mic permission. Note the inversion the offline design creates: **exfiltration
cannot use the network, so I stage to disk and collect later, or write to the same USB
stick the deck came from.** Offline does not stop exfiltration, it changes its shape.

**Mitigation (concrete).**
1. **Compile-time hardening, all of it, verified in CI:** `-D_FORTIFY_SOURCE=3 -O2`,
   `-fstack-protector-strong`, `-fstack-clash-protection`, `-Wl,-pie`, RELRO/BIND_NOW where
   applicable, and `-fsanitize=bounds`/UBSan in debug + CI test builds. Add
   `-ftrivial-auto-var-init=zero`. These are CMake flags, not aspirations — make them a
   failing CI check.
2. **ASan + UBSan + libFuzzer harnesses over the three parsing entry points** (package
   reader, XML→slide-model builder, font pre-validator), run in CI on a corpus of malformed
   `.pptx` fixtures. This is the control that actually finds the bug before the attacker.
3. **Reduce the codec surface** — the image-plugin allowlist in TM-015 removes libtiff,
   libwebp, and giflib from the process entirely. Fewer parsers, fewer bugs.
4. **macOS hardened runtime + App Sandbox with a minimal entitlement set**
   (`com.apple.security.device.audio-input`, user-selected read-only file access) and
   **no** entitlement for network (`com.apple.security.network.client` must be absent — the
   product forbids network anyway, so the sandbox can state it, converting a product rule
   into an OS-enforced one). Sandbox + hardened runtime turns TM-021 from "game over" into
   "attacker is inside a container with no network and no arbitrary file write."
5. **Post-MVP, the structurally correct fix:** parse the deck in a separate, sandboxed
   helper process that returns a serialized slide model over a pipe. Record this now as the
   security-architecture end state; ADR it when scope allows.

#### TM-022 — Dylib hijacking of the LGPL-mandated dynamic Qt frameworks
**Attack.** ADR-0001 requires **dynamic** Qt linking for LGPL compliance — the license
posture is also the attack surface. `macdeployqt` puts QtCore/QtGui/QtWidgets in
`Contents/Frameworks` resolved via `@rpath`/`@executable_path`. With one file write (from
TM-021, or a USB "update," or the app living in a user-writable directory), I replace
`QtGui.framework/Versions/A/QtGui` with a shim that `dlopen`s the real one and also starts
recording the microphone. Unsigned, un-notarized, no library validation → **the loader
accepts my library without complaint.** I now run in-process on every launch with full
access to the deck and the mic, and there is no integrity check anywhere that would notice.

**Mitigation (concrete).**
1. **Hardened runtime with library validation** (`--options runtime`, no
   `com.apple.security.cs.disable-library-validation`) — the OS then refuses to load any
   dylib not signed by the same Team ID. This is the direct, complete fix and it requires
   the $99 Developer ID (Platform Module §3.3).
2. **Notarize the DMG.** Notarization also makes tampering with the bundle detectable by
   Gatekeeper on subsequent launches.
3. **Sanitize the dynamic-loader environment at startup**, before anything else: refuse to
   run (or re-exec cleanly) if `DYLD_INSERT_LIBRARIES`, `DYLD_LIBRARY_PATH`, or
   `DYLD_FRAMEWORK_PATH` is set. (Hardened runtime already strips these; do it anyway for
   unsigned dev builds, which is exactly when the risk exists.)
4. **Do not resolve static-linking as the fix** — statically linking Qt would break the
   LGPL posture recorded in ADR-0001 ("dynamic linking only, no static Qt"). The tension is
   real and the resolution is signing, not linkage. Record it so nobody "fixes" TM-022 by
   creating a licensing violation.
5. Ship a `verify-bundle.sh` that runs `codesign --verify --deep --strict` and
   `spctl -a -vvv` and prints the result — run it as part of the pre-talk ritual.

#### TM-023 — Qt plugin-path hijacking (`qt.conf`, `QT_PLUGIN_PATH`) → code execution at startup
**Attack.** Very Qt-specific and often missed. At `QApplication` construction, Qt resolves
plugin directories from, in order: a `qt.conf` next to the executable / in `Contents/
Resources`, the `QT_PLUGIN_PATH` environment variable, and compiled-in paths. Qt then
`dlopen`s whatever platform and imageformats plugins it finds. Two ways in: **(a)** I write
`QT_PLUGIN_PATH=/Users/karl/.cache/x` into `~/.zshenv` (one line, easy to hide) — every
subsequent launch from a terminal loads my `libqevil.dylib` as an "image format plugin,"
with code running before `main()`'s logic does anything. **(b)** I drop a `qt.conf` into
the bundle's `Resources` (TM-022's write primitive) pointing `Plugins` at a directory I
control. Either way I get in-process execution without touching a single Qt binary, so a
hash check of the frameworks finds nothing.

**Mitigation (concrete).**
1. **Call `QCoreApplication::setLibraryPaths({bundlePluginDir})` before constructing
   `QApplication`** — this is the documented way to pin plugin resolution and it must
   happen before the app object exists, or platform-plugin loading has already consulted
   the environment.
2. **Unset `QT_PLUGIN_PATH`, `QT_QPA_PLATFORM_PLUGIN_PATH`, `QT_QPA_PLATFORM`,
   `QT_DEBUG_PLUGINS`, and `DYLD_*` at the top of `main()`** (before Qt initialization),
   using `unsetenv()`. Six lines that close (a) entirely.
3. **Ship an explicit `qt.conf`** in `Contents/Resources` pinning `Plugins = PlugIns` — a
   file we control that exists is safer than an absent file an attacker can create.
4. **Ship only the plugins we need** (`cocoa` platform plugin, `qpng`/`qjpeg` imageformats;
   see TM-015) and hash-verify the plugin directory alongside the model manifest (TM-006).
5. Library validation (TM-022) covers this too once signed — plugins are dylibs.

---

## 1.4 Multi-step attack chain (TA-1 → full compromise)

The chain deliberately crosses four STRIDE categories, uses only capabilities established
above, and never once touches the network.

**Step 1 — Delivery (spoofing the context, not the identity).** Two days before the talk I
email Karl `Board_Review_2026-08-10_v7_FINAL.pptx` from a plausible internal-looking
address, subject "updated numbers on slide 9 per this morning." He is a week from a live
executive presentation and this is exactly the file he is expecting. He opens it in
powerpoint-voice to check that it renders. **[TM-003 context, TM-004/TM-007 surface]**

**Step 2 — Execution (E via D's surface).** The deck is a perfectly valid OOXML package —
it passes the ≤200 MB cap, the ≤300-slide cap, the 1 GB decompressed cap, every part-name
check. Slide 9's text is set in an embedded font whose `CFF ` charstring is crafted to
overflow the font engine's operand stack. `QRawFont::loadFromData()` hands it to CoreText,
inside the app's process, unsandboxed. I get code execution as Karl's user.
**[TM-016 → TM-021]**

**Step 3 — Persistence (tampering the app against itself).** My shellcode does two writes
and exits cleanly so the deck renders normally and Karl notices nothing. Write one:
`~/Library/LaunchAgents/com.apple.fontcache.plist`, a launch agent surviving reboot. Write
two: into the app bundle — which is unsigned, so nothing validates it — a replacement
`QtGui` framework binary, plus a `qt.conf` pointing plugins at my directory as a fallback
foothold. **[TM-022 + TM-023]**

**Step 4 — Collection (information disclosure, no network needed).** On every subsequent
launch my library loads in-process and inherits the app's already-granted TCC microphone
permission and its in-memory slide model. It writes the *real* board deck and a rolling
audio capture — including the live executive talk — into an innocuously named file under
`~/Library/Caches/`. Exfiltration is by staging, not transmission: I collect it whenever I
next have access, or it rides out on the next USB stick or backup. **The offline
architecture removes my exfil channel; it does not remove my exfil.**
**[TM-011 asset exposure]**

**Step 5 — Optional finale (denial of service, precisely timed).** If disruption is worth
more than data, the same foothold sets a timer for 09:38 on 2026-08-10 and sends the
process `SIGKILL` eight minutes into the talk — with the projector on, in front of the
audience, and no crash-position restore in the MVP (Manifesto Q11, accepted). **[A1]**

**Where the chain breaks, in order of leverage:**
1. **Step 2** — the font pre-validator (TM-016.1) rejects the malformed `CFF` before
   CoreText sees it; and hardened-runtime + App Sandbox (TM-021.4) contains the execution
   even if the validator misses.
2. **Step 3** — library validation from a Developer ID signature (TM-022.1) makes the
   bundle un-writable in practice and the replacement dylib unloadable.
3. **Step 4** — App Sandbox with no network entitlement and user-selected-file-only access
   confines the collection stage.
4. **Step 5** — nothing in the MVP mitigates this once Step 2 succeeds. That is the
   argument for spending the mitigation budget at Step 2.

---

## 2. Architecture Stress Test

### 2.1 Five edge cases where this stack fails

**E1 — Embedded fonts arrive obfuscated, and QRawFont rejects all of them.**
PowerPoint stores embedded fonts as `fntdata` parts obfuscated by XORing the first 32 bytes
with the GUID taken from the part name. `QRawFont::loadFromData()` sees a corrupt sfnt
header and returns an invalid font. If Phase 2 does not implement the deobfuscation step,
**every embedded font in every real deck fails**, every glyph falls back to substitution,
metrics change, and text overflows its shape on slides that looked correct in PowerPoint.
The failure is *total* and looks like "the renderer is broken," and it will only be
discovered against a real deck — i.e. at rehearsal, with days to spare. **Trigger:** any
deck saved with "Embed fonts in the file" checked, which is standard practice for decks
sent to a projector. **Detection:** load the real deck early in Phase 2, not in Phase 3.

**E2 — Pasted Excel charts and Visio diagrams are EMF/WMF, which Qt cannot decode.**
Office embeds pasted charts as EMF (with a fallback PNG only sometimes). Qt has no EMF/WMF
support on any platform. Every such object becomes a visible placeholder. On a finance or
strategy deck this can be *the majority of the content on the most important slides*. The
renderer is behaving exactly as designed (§4.1-1 placeholders) and the outcome is still
unacceptable to the user. **Trigger:** one pasted chart. **Mitigation available:** prefer
the OOXML alternate-content PNG/JPEG fallback part when present — implement
`mc:AlternateContent` fallback resolution, which converts many of these into correct
rendering for free.

**E3 — Display hot-plug changes devicePixelRatio mid-show and the pre-rendered rasters are
now wrong.** Karl presents on a Retina laptop (DPR 2.0) with a 1080p projector (DPR 1.0).
TM-018's mitigation pre-renders slides to rasters at load — at whichever DPR was current.
On hot-plug, `QScreen::physicalDotsPerInch`/DPR changes; Qt may recreate the native window,
dropping full-screen state and **exposing the desktop, and whatever else is on it, to the
room**. Cached rasters are either blurry (upscaled) or wasteful (downscaled), and the
"pixel-stable" contract silently breaks. **Trigger:** plugging the HDMI cable in after
opening the deck — the most common possible sequence. **Mitigation:** key the raster cache
by `(slideIndex, targetSize, dpr)`, re-render on `QScreen::physicalDotsPerInchChanged` /
`screenChanged`, and re-assert full-screen + slide index on every screen change (§4.1-7
already requires position preservation).

**E4 — macOS TCC microphone permission silently denied for an unsigned, rebuilt binary.**
TCC keys the microphone grant to the app's code identity. An unsigned binary is identified
by path plus cdhash, so **every rebuild is a new app** to TCC. The consent dialog is a
system modal that can appear *behind* a full-screen window, or be suppressed entirely if
the user previously denied. The result is an app that runs perfectly, shows the listening
glyph, and receives nothing but silence. **Trigger:** `cmake --build` followed by a launch
into full-screen. **Mitigation:** ad-hoc-sign with a stable identifier from day one; check
`AVCaptureDevice.authorizationStatus` explicitly at startup and render an in-app
"microphone permission not granted — open System Settings" state (distinct from
"mic unavailable"); never enter full-screen before the permission state is known. Fold
this into the Q4 pre-show voice check.

**E5 — `vosk-model-small-en-us-0.15` under real room acoustics with a laptop mic.**
The small model is ~40 MB and trades accuracy for size. In a 10 m room with HVAC, a laptop
array mic at 2 m, and a presenter who turns toward the screen, word error rate degrades
sharply — and the §2.3 exit criterion is **≥95 % command recognition**. The grammar helps
(closed vocabulary), but a grammar cannot recover phonemes the mic never captured.
**Trigger:** the actual venue, discovered at rehearsal on 2026-08-08, two days before the
talk. **Mitigation:** the fallback is already recorded in ADR-0001 (keyboard-primary
presentation; Option C as a v1.1 experiment) — plus the cheapest real fix, a wired lapel or
headset mic, which changes SNR by ~20 dB and should be tested at rehearsal alongside the
laptop mic.

### 2.2 Three security vulnerabilities inherent to this design

**V1 — Four memory-unsafe parsers, attacker-controlled input, one process, no isolation.**
libzip + pugixml + Qt image codecs + CoreText/FreeType all consume attacker bytes inside
the process that holds the Confidential deck, the live microphone handle, and the TCC
grant. There is no sandbox and Qt provides no isolation primitive. A single heap overflow
anywhere in that set yields everything (TM-021). This is *inherent* to the "one native
process parses untrusted files" architecture; the only structural fix is a separate
sandboxed parser process, which is post-MVP. Until then the compensating controls are
hardened runtime + App Sandbox + surface reduction + fuzzing.

**V2 — LGPL-mandated dynamic linking plus an unsigned bundle equals an open plugin/dylib
loading path.** ADR-0001 requires dynamic Qt linking for LGPL compliance; the Platform
Module permits unsigned MVP distribution. Together they produce an app that will load any
dylib placed in its Frameworks directory and any plugin pointed at by an environment
variable, with no integrity check (TM-022, TM-023). Neither decision is wrong; their
*combination* is the vulnerability, and it is resolved by signing — not by relinking.

**V3 — A closed grammar authenticates the room, not the presenter, and can be made to
over-match.** Vosk's grammar-constrained mode is a nearest-match decoder: without `[unk]`
in the grammar, arbitrary acoustics are forced onto the closest command phrase, so the
control chosen to *reduce* false triggers can amplify them (TM-001). And no component in
this stack performs speaker verification — the small en-us model has no speaker embedding
and none is configured — so any voice in the room is an authenticated operator. The command
channel is, by construction, unauthenticated and broadcast.

### 2.3 Two storage/parsing bottleneck risks with trigger conditions

**B1 — Whole-deck-in-RAM slide model plus decoded images, with no disk cache permitted.**
The data contract forbids writing deck content to disk *at all* (no thumbnail cache, no
temp extraction), so the standard mitigation for a large deck — spill to disk — is
unavailable by design. TM-018's mitigation makes it worse by pre-rendering every slide.
**Trigger condition:** a legitimate 180 MB deck, 250 slides, each with a full-bleed
3840×2160 photo. Decoded RGBA is 3840 × 2160 × 4 ≈ **33 MB per image**; 250 of them is
**≈8.3 GB**, before pre-rendered rasters (a 3840×2160 raster is another 33 MB/slide). On a
16 GB MacBook this reaches memory pressure, then swap, then the jetsam killer — and the
<200 ms slide-change contract dies long before the process does. **Detection:** the
cumulative decode budget in TM-015.5. **Mitigation:** never retain full-resolution decoded
images — downsample to the target display resolution at decode time and free the original;
keep an LRU raster cache of bounded size (suggest 512 MB) with ±3-slide prefetch; store the
slide model (a few hundred KB/slide) for everything, rasters for a window. Re-rendering a
cache miss is bounded by the TM-018 deadline, so a miss degrades latency, never
availability.

**B2 — Load-time full validation collides with the two-minutes-before-the-talk workflow,
and there is no warm path.** TM-018 and TM-014 both push work to load time (validate all
parts, pre-render all slides, verify all fonts), and the no-disk-cache rule means **this
work is redone in full on every single open** — there is no warm start, ever. **Trigger
condition:** ≥120 slides with images pushes cold load past ~30 s on a laptop that is also
running Zoom and Slack; at ≥250 slides it is minutes. The user-visible failure is Karl
double-clicking his deck at 09:28 for a 09:30 talk and watching a progress bar. **This is a
self-inflicted availability risk created by the security mitigations**, which is exactly
why it belongs in the threat model. **Mitigation:** two-stage load — parse + validate all
parts and render slides 1–5 synchronously behind a progress indicator with a slide count
and elapsed estimate, then continue rendering the remainder on a background thread while
the presenter is already able to start; the load-time warning report (Output 4) updates
live as background rendering completes, and any slide not yet rendered when reached is
rendered on demand under the same deadline. Non-negotiable: **the progress indicator must
be honest and cancellable**, because "the app is frozen" and "the app is working" must
never look the same two minutes before a talk.

### 2.4 One limitation that could force a rewrite within 12 months

**The from-scratch OOXML renderer's fidelity fence is a product-level cliff, not a backlog
item.** The MVP contract is a text+images tier with visible placeholders for everything
else (§4.1-1, ADR-0001 Consequences). That is honest and it holds for exactly as long as
Karl authors his own decks. The first deck he does *not* control — one built by a finance
team, a consultancy, or corporate marketing — will contain tables, DrawingML charts,
SmartArt, grouped/rotated shapes with effects, and pasted EMF (E2). Those are not edge
cases in executive decks; they are the substance of the important slides. "Placeholder"
then means "the slide with the numbers is blank," which is a product failure, not a
degraded mode.

At that point there are two exits and neither is an increment:
- **Implement DrawingML properly** — charts, tables, SmartArt layout, shape effects,
  theme/master/layout inheritance, and the OOXML placeholder-inheritance chain. This is
  a multi-person-year body of work, comfortably the largest component in the product, and
  it does not fit a solo maintainer's schedule at any velocity.
- **Replace the renderer with a rendering engine** — embedded LibreOffice/UNO (the
  recommendation ADR-0001 explicitly overrode), or a PowerPoint/LibreOffice-driven
  pre-render to PDF/PNG consumed as images. Either **rewrites the entire input path** and
  with it every mitigation in §1's Tampering/DoS sections: LibreOffice is a vastly larger
  untrusted-input attack surface than libzip+pugixml, its licensing (MPL/LGPL) changes the
  compliance posture, its footprint destroys the ~50–80 MB packaging story, and the
  parsing-security work in TM-004/007/014/015/016/017 is discarded and must be redone
  against a different engine.

**Early-warning signal to watch, and the decision it should trigger:** track the
placeholder count in the load-time warning report across every real deck opened. When a
deck Karl must present exceeds roughly 10 % placeholder slides, the fidelity fence has
failed for his actual use, and the choice above must be made deliberately — with an ADR —
rather than discovered mid-week before a talk. That metric is free: the warning report
already counts them.

---

## 3. Risk / Mitigation Matrix

Severity = impact on the ranked assets (A1 availability during the live talk is weighted
highest) × likelihood given the stated actors. **Phase 3 obligation** names the validation
method that must appear in `docs/test-results/YYYY-MM-DD_threat-model-validation.md`
(template: `templates/generated/threat-model-validation.tmpl`).

| ID | STRIDE | Threat (short) | Actor | Asset | Likelihood | Impact | **Severity** | Primary mitigation | Already decided? | Phase 3 obligation |
|---|---|---|---|---|---|---|---|---|---|---|
| TM-018 | D | Mid-deck render bomb — legal OOXML that hangs the UI thread on slide 13 | TA-1 | A1 | Medium | Critical | **Critical** | Pre-render all slides at load, off-thread, hard per-slide deadline → placeholder; per-slide complexity caps | New (extends F1) | Craft a 200k-run slide; assert placeholder + UI responsive; assert all-slides pre-show report |
| TM-021 | E | Memory-safety exploit in libzip/pugixml/QImage/font path → code exec as Karl | TA-1 | A5,A3,A4 | Low-Med | Critical | **Critical** | Hardened runtime + App Sandbox (no network entitlement); compile hardening; fuzz all three parser entry points; codec surface reduction | New | ASan/UBSan fuzz runs archived; `codesign -d --entitlements` output archived |
| TM-016 | D/E | Malformed embedded font → CoreText crash or heap corruption | TA-1 | A1,A5 | Medium | Critical | **Critical** | sfnt pre-validator (table bounds, counts, size cap) before QRawFont; font-load deadline; fail to the existing visible substitution path | Partly (§11-3 substitution) | Fuzz the font pre-validator; malformed-`CFF` fixture must substitute, not crash |
| TM-014 | D | Zip/decompression bomb; compression-method confusion | TA-1 | A1 | High | High | **High** | Streaming `zip_fread` with per-part + 1 GB total + entry-count budgets; method allowlist {0,8}; ratio circuit-breaker; never trust `zip_stat` | Yes (Manifesto Q9) | 42.zip-class and zstd-method fixtures rejected by name; app stays running |
| TM-001 | S | Audience voice injection; grammar over-match without `[unk]` | TA-2 | A1,A2 | High | High | **High** | `[unk]` mandatory in the Vosk grammar; per-word confidence gate; two-word grammar; pause enforced in the dispatcher; directional mic | Partly (Q1, Q5) | Assert `[unk]` present; babble fixtures → no dispatch; paused-state dispatch test |
| TM-015 | D | Image pixel bomb / vulnerable codec via QImage | TA-1 | A1,A5 | High | High | **High** | `QImageReader` header-first + pixel budget; explicit `setAllocationLimit`; PNG/JPEG plugin allowlist (drop tiff/webp/gif); cumulative decode budget | New | 65535×65535 PNG fixture → placeholder, RSS bounded; assert bundled plugin list |
| TM-022 | E | Dylib hijack of dynamically linked Qt frameworks | TA-3 | A5,A3,A4 | Low-Med | Critical | **High** | Developer ID signing + hardened runtime **with library validation** + notarization; scrub `DYLD_*`; never "fix" by static Qt (LGPL) | New | `spctl -a -vvv` + `codesign --verify --deep --strict` archived |
| TM-004 | T | Zip-slip / symlink escape via OOXML part names | TA-1 | A5 | Medium | High | **High** | No-disk-extraction enforced by a single read path + Semgrep rule; part-name canonicalization; reject symlink/dir entries | Yes (G-6) | Traversal + symlink fixtures rejected; Semgrep rule green |
| TM-011 | I | Deck + live audio exfiltrated via core dumps / `.ips` reports | TA-1,TA-3 | A3,A4 | Medium | High | **High** | `RLIMIT_CORE=0` first in `main()`; arena + zeroizing fatal-signal handler; audio buffers overwritten; no deck content in any diagnostic string | Yes (Q8) | Force SIGSEGV with deck loaded; assert no core and no deck bytes in the crash report |
| TM-023 | E | Qt plugin-path hijack via `qt.conf` / `QT_PLUGIN_PATH` | TA-3 | A5 | Medium | High | **High** | `setLibraryPaths()` before `QApplication`; `unsetenv` of `QT_*`/`DYLD_*` at top of `main()`; shipped `qt.conf`; minimal plugin set | New | Launch with hostile `QT_PLUGIN_PATH`; assert the plugin is not loaded |
| TM-017 | D | XML DOM amplification + recursive-walker stack overflow | TA-1 | A1 | Medium | High | **High** | Depth cap 100 in the walker; 50 MB per-XML-part cap; node ceiling; parse on a worker thread with an explicit stack | New | 200k-deep fixture → placeholder + warning, no crash |
| TM-019 | D | Audio pipeline wedge (Vosk on the audio callback; device unplug) | TA-2 / accident | A1 | Medium | Medium | **Medium** | Three-thread design with a lock-free ring; 2 s frame watchdog → banner; device-change re-init; bounded engine restarts | Partly (§4.1-2/3, G-8) | Chaos test: unplug mic mid-show; banner appears, keyboard unaffected |
| TM-007 | T | Duplicate/ambiguous zip entries → silently wrong slide | TA-1 | A2 | Low-Med | High | **Medium** | Single canonical index built once; reject duplicate names (NFC + case-insensitive); reject trailing data; parse only relationship-reachable parts | New | Duplicate-entry fixture rejected by name |
| TM-020 | D | Acoustic denial; hostile "pause presentation" wedge | TA-2 | A1 | Medium | Medium | **Medium** | Keyboard parity (F6); pause/continue rate limit; any key exits pause; persistent PAUSED indicator; pre-show SNR check | Partly (F6, Q6) | Noise-flood fixture; assert full keyboard drivability |
| TM-002 | S | Recorded-audio playback injection under acoustic cover | TA-2 | A1,A2 | Medium | Medium | **Medium** | Command rate limit (3/5 s, ≥700 ms apart); reverse-direction confirmation; overlay always shows heard + done | New | Rapid-playback fixture; assert throttling + overlay notice |
| TM-012 | I | Overlay projects heard room speech onto the audience display | TA-5 | A4 | High | Medium | **Medium** | Split overlay by display role: command token + glyph on the projector, full heard text on the laptop only; length cap; 3 s fade | New (refines Q5) | Dual-display test: assert no heard text on the external screen |
| TM-005 | T | Settings tampering disarms the keyboard fallback | TA-3 | A1 | Low | High | **Medium** | Defaults always live (config may add, never remove); five-command binding invariant unit-tested; range clamping; revalidate on change | Partly (G-3) | Hostile settings fixture; assert defaults survive and all five commands work |
| TM-006 | T | Bundled Vosk model / grammar tampering | TA-3,TA-4 | A1,A2 | Low | High | **Medium** | SHA-256 manifest compiled into the binary; grammar is a `constexpr` literal, not a resource; fail closed to mic-unavailable | Partly (Input 4) | Corrupt a model file; assert banner + keyboard fallback, no crash |
| TM-003 | S | Trojaned build spoofing the product | TA-1,TA-4 | A5 | Low | Critical | **Medium** | Signing + notarization; CI-published SHA-256 per artifact; in-app commit hash on the pre-show screen | New | Verify published hash matches CI; assert build stamp is visible |
| TM-010 | I | XXE / billion laughs via the XML layer | TA-1 | A3,A1 | Very Low | High | **Low** (invariant) | pugixml does not process DTDs or expand custom entities — structurally immune; locked-in negative test + parser-change ADR gate + explicit parse flags | New (invariant) | XXE + billion-laughs fixtures: assert no expansion, no file read, bounded memory |
| TM-013 | I | Deck filenames disclosed via holding screen / window title | TA-5 | A3 | Medium | Low | **Low** | No filenames or recent list on the audience-facing holding screen; constant window title in presentation mode; basename-only, bounded, clearable | New (refines Q3) | Dual-display test: assert no filename on the external screen |
| TM-008 | R | Slide changes unattributable after an incident | TA-2 | A2 | High | Low | **Low** | Always-on in-memory command log with `source` (voice/keyboard), confidence, latency, correlation ID; user-invoked export; never deck content | Partly (Q7) | Assert `source` recorded for both paths; assert no deck content in the export |
| TM-009 | R | No record of which deck bytes were rendered | TA-3 | A2 | Low | Medium | **Low** | SHA-256 + size + mtime of the `.pptx` logged at load; short hash on the pre-show screen only | New | Assert hash logged and matches; assert it never renders on the audience display |

**Coverage check (Phase 1→2 structural validation):**

| STRIDE | Threats | IDs |
|---|---|---|
| **S** — Spoofing | 3 | TM-001, TM-002, TM-003 |
| **T** — Tampering | 4 | TM-004, TM-005, TM-006, TM-007 |
| **R** — Repudiation | 2 | TM-008, TM-009 |
| **I** — Information Disclosure | 4 | TM-010, TM-011, TM-012, TM-013 |
| **D** — Denial of Service | 7 | TM-014, TM-015, TM-016, TM-017, TM-018, TM-019, TM-020 |
| **E** — Elevation of Privilege | 3 | TM-021, TM-022, TM-023 |
| **Total** | **23** | TM-001 … TM-023 |

- [x] Every STRIDE category has ≥1 threat
- [x] Every threat references a specific component or data flow in this architecture
      (libzip, pugixml, QImage/QImageReader, QRawFont/CoreText, Vosk grammar, miniaudio,
      Qt plugin loading, the settings JSON, the bundled model, the overlay/display split)
- [x] Every mitigation is a concrete technical control (API call, cap with a number, build
      flag, entitlement, test, or CI rule) — no "validate input," no "be careful"
- [x] At least one multi-step attack chain (§1.4, five steps, TM-016 → TM-021 → TM-022/023
      → TM-011 → A1)
- [x] Stable IDs TM-001…TM-023, registered in `docs/IDENTIFIERS.md` under the `TM-` prefix
- [x] Network-attacker threats explicitly scoped out with rationale (§0), not padded in

**Highest-severity threat to a live presentation: TM-018.** It uses entirely valid OOXML,
so every input-validation control in this document passes it; it is positioned mid-deck, so
it survives any pre-show check that looks at slide 1; and it lands on A1, the asset the
product exists to protect. **Highest-severity threat to the machine: TM-021**, reached most
cheaply through TM-016.

**Carried into Phase 2 as new build obligations** (not already covered by an existing
Manifesto decision): TM-002, TM-003, TM-007, TM-009, TM-010 (as an invariant test),
TM-012, TM-013, TM-015, TM-017, TM-018, TM-021, TM-022, TM-023 — plus the strengthened
implementations of TM-001, TM-004, TM-005, TM-006, TM-008, TM-011, TM-014, TM-016, TM-019,
TM-020 noted in the "Already decided?" column.
