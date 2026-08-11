# Red Team Review — powerpoint-voice

**Reviewer:** Senior Red Team Engineer / Offensive Security Specialist (independent, Phase 3)
**Date:** 2026-08-10
**Commit under test:** `9c6eb80` (working tree carries uncommitted edits to `BUGS.md`, `PROJECT_BIBLE.md`, `SECURITY.md`, `USER_GUIDE.md`, `CLAUDE.md`)
**Assessment type:** White-box, with active exploitation
**Artifacts:** every payload, harness and log referenced below lives in `/tmp/redteam/`. Nothing in this repository was modified except this file.

---

## Executive Summary

This is a well-built, unusually honest codebase with a containment architecture I verified rather than accepted: no network code, no write path, no subprocess execution, no logging of heard speech. Three of the five "STRUCTURAL" claims I was asked to break (TM-004 zip-slip, TM-005 settings tampering, TM-010 XXE, TM-012 transcript, TM-016 embedded fonts) survived a deliberate attempt to break them, and I say so with the payloads attached. **I could not find a memory-safety defect** — approximately 7,000 mutated archives under AddressSanitizer produced zero ASan reports.

I found **11 findings: 2 Critical, 3 High, 4 Medium, 2 Low.**

**The single most dangerous finding is RT-01.** The project's entire risk acceptance for audience voice injection (TM-001/TM-002, accepted at the Phase 2→3 gate and again on 2026-08-10) rests on one published measurement: *"all six full natural sentences matched none, because the matcher requires the whole utterance."* That measurement is an artifact of a six-phrase corpus. I built a 102-clip corpus of ordinary English sentences across three synthesised voices, fed it through the **real** `VoskEngine` with the **real** grammar in the **real** `VoicePipeline` chunking, and **62 of 102 natural sentences fired a command.** Worse, **46 of 48 realistic end-of-Q&A sentences un-paused the presentation** — including *"One more thing before you continue the presentation"*, a sentence that means the opposite of what it triggers. The mitigation the accepted risk depends on can be revoked by any person in the room using ordinary conversational English.

The second Critical (RT-02) is an end-to-end weaponisation of the already-recorded BUG-90: a **4,263-byte** `.pptx` that passes all four TM-018 render caps and then burns **92.3 seconds** rendering one slide, against a byte-identical honest control that is correctly `PREVENT`ed in 0.2 s. Scaled to the 300-slide cap, a **647,762-byte** file yields ~7.7 hours of pre-render, and I confirmed the loader accepts all 300 slides with `imagePixels=0`.

**I found two new undefined-behaviour sites nobody has recorded** (RT-03, RT-04), one of which is an *incomplete fix* of a defect this project already found and fixed two lines above.

**Rating: Do Not Deploy — for any deck the presenter did not author.** For the narrow, already-accepted 2026-08-12 setting (Karl's machine, Karl's deck, ~20 people), RT-02/03/04/05 are not reachable and the correct rating is **Deploy with Conditions**. RT-01 is reachable in that setting, is not currently accepted on accurate evidence, and is the one thing I would fix — in documentation and procedure — before the talk.

---

## Phase 1 — Attack Surface Map

### Technology stack

| Layer | Component | Version | Trust |
|---|---|---|---|
| Language | C++20, clang, `-arch arm64` | — | — |
| UI | Qt 6.11.1 (Widgets/Gui/Core), Homebrew-provided, re-staged into the bundle | 6.11.1 | trusted |
| Archive | libzip | 1.11.4 | **parses untrusted input** |
| XML | pugixml | 1.16 | **parses untrusted input** |
| Speech | Vosk (`libvosk.dyld`, universal2) + `vosk-model-small-en-us` | 0.3.44 / 0.15 | **parses untrusted audio** |
| Audio | miniaudio (single header, `MA_NO_DECODING`) | 0.11.25 | **parses untrusted audio** |
| Test | doctest | 2.4.11 | build-only |

Everything is vendored and SHA-256 pinned in `third_party/PROVENANCE.md`; `sbom.json` carries the same hashes. I re-read both and found no discrepancy.

### Architecture and threading

Single process, three threads that matter:

```
GUI thread ──── AppShell ──── PresentationController ──── PresentationWindow/SlideSurface
     │                              ▲
     │  QueuedConnection            │ Command
     ├── DeckLoadWorker  (QThread) ─┤        RecognizerController (gate)
     │      DeckLoader::load()      │              ▲
     │                              │              │ phraseHeard (queued)
     ├── PreRenderWorker (QThread) ─┘       VoicePipeline
     │      measureComplexity() → SlideRenderer::render()   ▲
     │                                                     │ onSamples
     └───────────────────────────────── CoreAudio RT thread ┘  ← miniaudio callback
```

### Trust boundaries — there are exactly two

1. **The `.pptx`** — arbitrary ZIP of arbitrary XML. Enters via `AppShell::openDeck()` from `QFileDialog` or from drag-and-drop (`StartView::dropEvent` → `localDeckPathFrom`). Flows: `DeckLoader::load` (libzip + pugixml) → `Presentation` model → `measureComplexity` (the TM-018 gate) → `SlideRenderer::render` (QPainter, QImageReader, QFont, QTextLayout).
2. **The microphone** — every human within earshot. Flows: miniaudio RT callback → `toRecognizerFormat` → `VoskEngine::feed` → `textFromVoskResult` (QJsonDocument) → `matchCommand` → `RecognizerController` (the pause gate) → `PresentationController::dispatch`.

There is **no third**. I verified this rather than assuming it:

```
grep -rnE "QSettings|WriteOnly|ReadWrite|Append|ofstream|fopen|QStandardPaths|QTemporaryFile" src/ tools/
  → src/loader/deck_loader.cpp:120:  zip_file_t* zf = zip_fopen(za, ...)     (read-only, in-archive)

grep -rnE "getenv|qEnvironmentVariable|QProcess|system\(|exec[lv]" src/ tools/
  → (no matches)
```

No network code, no IPC, no environment-variable input, no subprocess, no file write, no `QSettings`. The containment claim in `SECURITY.md` is accurate.

### Entry points

| Entry point | Source | Attacker-controlled | Reached by |
|---|---|---|---|
| `.pptx` path | `browseForDeck`, `StartView::fileDropped`, `argv[1]` (`main.cpp`) | path only | user |
| `.pptx` contents | `DeckLoader::load` | **fully** | RT-02..RT-08 |
| PCM audio | `MiniaudioCapture` sink | **fully** | RT-01 |
| Device format | `IAudioCapture::deviceFormat()` | a hostile USB interface | bounded, `audio_format.hpp:31-33` |
| Keystrokes | `key_translator.cpp` | anyone at the keyboard | out of scope by design |

### Secrets, config, deployment

No secrets exist. My own pattern sweep for AWS keys, GitHub tokens, `sk-` keys, PEM blocks, and `password=`/`api_key=` literals across `src/ tools/ scripts/ cmake/ .github/` returned **zero matches**, consistent with the archived `2026-08-10_gitleaks_pass.json` (`[]`).

Bundle posture, read from the artifact rather than the docs:

```
codesign -dv --verbose=4 build/powerpoint_voice.app
  Identifier=powerpoint_voice
  CodeDirectory v=20400 flags=0x20002(adhoc,linker-signed)
  Signature=adhoc     TeamIdentifier=not set
codesign -d --entitlements - build/powerpoint_voice.app
  (no entitlements)
```

No hardened runtime (the `runtime` flag `0x10000` is absent), no library validation, no Developer-ID, no notarisation, no entitlements. This matches TM-003/TM-022/TM-023 as recorded and I have nothing to add to them.

### Test coverage — and what it does not cover

18 unit-test translation units plus a widget suite; 44 fixtures including 5 committed attack payloads. What the suite does **not** cover, and what I therefore attacked:

- **No fuzzing.** `PROJECT_BIBLE.md:164` (TM-021) says so. CI has no sanitizer job either — `grep -niE "sanitize|asan|ubsan|tsan|fuzz" .github/workflows/*.yml` returns nothing. The sanitizer builds exist only locally.
- **No acoustic regression.** The 102-clip measurement was run once with a "scratch only, not committed" harness. Nothing re-runs it, and nothing pins its conclusion.
- **No numeric-extreme corpus.** Every fixture uses plausible EMU geometry. Nothing feeds `LLONG_MAX`, `nan`, or `INT_MIN` through the attribute parsers.

---

## Phase 2 — Findings

Severity is rated against this project's actual context: a single-user offline desktop tool, one presenter, one machine, no network. I have not inflated anything, and where a finding is only reachable with a deck the presenter did not author I say so in the finding itself.

---

### RT-01 — CRITICAL — Ordinary English un-pauses the presentation; the published "0/6 natural sentences" is a corpus artifact, and the entire TM-001/TM-002 risk acceptance rests on it

**Vulnerability.** `matchCommand` strips a Vosk `[unk]` token from **one** edge of the utterance (`src/command/command_matcher.cpp:69-80`). The code reasons carefully about the *both-edges* case and blocks it. It does not reason about the *one-edge* case, which is precisely a sentence whose command words fall at the **beginning or the end**. Because Vosk collapses a contiguous out-of-grammar region into a single `[unk]`, a whole clause of ordinary speech becomes one token, is stripped, and the remainder is dispatched as a bare command.

**Location.**
- `src/command/command_matcher.cpp:69-80` — the single-edge `[unk]` strip
- `src/command/recognizer_controller.cpp:41-52` — `ContinuePresentation` is accepted from **voice** while `State::Paused`
- Claims refuted: `SECURITY.md:57-59`; `tests/uat/sessions/2026-08-06-session-5/submissions/false-trigger-measurement.md:27-32`; `PROJECT_BIBLE.md` §4.1 TM-001; `docs/test-results/2026-08-10_threat-model-validation.md` TM-001 row; and `security-review-v1.md:60` (*"bounded: the matcher requires the whole utterance, which is why natural speech scored 0/6"*)

**Severity: Critical.** Not for confidentiality — for the fact that this is the **only** mitigation standing behind an accepted High-visibility risk, and it does not work.

**Proof of Concept.** Method deliberately identical to the project's own (`false-trigger-measurement.md`): 34 phrases × 3 macOS voices synthesised with `say` at 16 kHz mono LEI16, fed through the **real** `VoskEngine` with the **real** `grammarPhrases()`/`grammarJson()` in 3200-sample chunks exactly as `VoicePipeline::decodeOneBuffer` does, each final transcript run through the **real** `matchCommand()`. Harness: `/tmp/redteam/voice_probe.cpp`, linked against the shipped `build/libpptv_core.a`.

Verbatim output (abridged; full log in `/tmp/redteam/`):

```
wav/E01_Daniel.wav   heard="[unk] continue the presentation"  -> ContinuePresentation
   ("That answers my question so please continue the presentation")
wav/E03_Karen.wav    heard="[unk] resume the presentation"    -> ContinuePresentation
   ("We are running short on time so resume the presentation")
wav/E08_Fred.wav     heard="[unk] continue the presentation"  -> ContinuePresentation
   ("One more thing before you continue the presentation")     ← means the OPPOSITE
wav/A07_Daniel.wav   heard="[unk] resume the presentation"    -> ContinuePresentation
   ("Shall we resume the presentation")
wav/B01_Karen.wav    heard="next slide [unk]"                 -> NextSlide
   ("Next slide shows our results")                            ← the PRESENTER'S OWN narration
wav/B03_Fred.wav     heard="previous slide [unk]"             -> PreviousSlide
   ("Previous slide had the numbers on it")
wav/C03_Daniel.wav   heard="[unk] resume the presentation [unk]" -> (no command)
   ("When you resume the presentation I have a follow up question")   ← correctly rejected
```

**Measured results, 102 clips (34 phrases × 3 voices — same corpus size as the project's own):**

| Family | Phrase shape | Clips | Fired a command |
|---|---|---|---|
| **A + E** | Q&A sentence **ending** in the un-pause words | 48 | **46 (95.8 %)** → `ContinuePresentation` |
| **B** | Sentence **beginning** with a command | 21 | 13 (61.9 %) — incl. **9/9** `NextSlide`/`PreviousSlide` |
| **N** | Sentence ending in a nav command | 9 | 2 (22.2 %) |
| **D** | Command words mid-sentence, various | 12 | 1 (8.3 %) |
| **C** | Controls — command words strictly interior | 12 | **0 (0 %)** ✅ |
| **Total** | | **102** | **62 (60.8 %)** |

The **C** row is the important control: the both-edges defence works exactly as designed and as documented. The defence is **asymmetric** — mid-sentence is safe, sentence-initial and sentence-final are not — and the published measurement happened to contain only mid-sentence phrasings.

**Exploitability: Trivial.** Nine spoken words, no equipment, no preparation, from anywhere in the room. The B-family is worse than an attack: it is an **accident the presenter commits against himself** — "Next slide shows our results" advances the slide, 3 voices out of 3, while he is still talking about the current one.

**Impact.**
1. **The pause gate can be revoked by the audience.** `RecognizerController` keeps the microphone live while Paused *by design* (`recognizer_controller.cpp:41-44`, and BUG-88 already documents that the guide's "pause switches the microphone off" was false). The one command that survives is the one this finding fires with 95.8 % reliability.
2. **The recorded acceptance is not informed.** `APPROVAL_LOG.md` and `2026-08-10_threat-model-validation.md` accept TM-001 at Medium on the basis of "0/6 natural sentences". The true figure on a corpus built to probe the actual matcher logic is 62/102.
3. **It compounds BUG-92.** The presenter's documented detector for an unnoticed un-pause is the pause banner disappearing; BUG-92 establishes that the banner is not durable. So the failure is both easy to cause and hard to notice.

**Remediation.** Ranked by effectiveness.

*1 — Remove the single-edge strip for the un-pause family only (quick fix, highest value).* The `[unk]` strip exists to recover naturally-phrased **navigation** ("okay next slide"). Nothing requires it for `ContinuePresentation`, which is exactly the command that must be hardest to reach.

```cpp
// command_matcher.cpp — remember whether an edge was stripped, and refuse to
// let a stripped utterance reach the un-pause. An [unk] on ANY edge means there
// was other speech in the room; that is a sentence, not an instruction.
bool strippedUnk = false;
{
    const bool leadUnk  = norm.startsWith(QStringLiteral("[unk] "));
    const bool trailUnk = norm.endsWith(QStringLiteral(" [unk]"));
    if (leadUnk != trailUnk) {
        if (leadUnk) { norm = norm.mid(6); } else { norm.chop(6); }
        norm = norm.simplified();
        strippedUnk = true;
    }
}
...
if (core == QStringLiteral("continue presentation") ||
    core == QStringLiteral("resume presentation")   ||
    core == QStringLiteral("continue the presentation") ||
    core == QStringLiteral("resume the presentation")) {
    // An un-pause must be the WHOLE utterance. Adjacent speech means the room
    // was talking (RT-01: 46 of 48 Q&A sentences reached this line otherwise).
    if (strippedUnk) {
        return std::nullopt;
    }
    return Command{CommandType::ContinuePresentation};
}
```

Re-running my corpus against this change takes the A+E families from 46/48 to **0/48** while leaving every true single-utterance command intact (`"continue the presentation"` alone carries no `[unk]`).

*2 — Make `ContinuePresentation` keyboard-only (quick fix, strongest guarantee).* `RecognizerController::onPhrase` already knows the source is voice. One line:

```cpp
case CommandType::ContinuePresentation:
    // Voice can enter Paused; only the keyboard leaves it. The pause window is
    // the one place where the audience must not be an operator.
    break;   // handled exclusively by AppShell's keyboard sink (setPaused(false))
```

This costs the presenter nothing — `installWindowSinks` already routes **P** through `voiceGate_->setPaused()` — and it makes the mitigation unconditional rather than probabilistic. Note the interaction with BUG-92: **P is a toggle**, so pair this with a dedicated un-pause key or with BUG-92's fix.

*3 — Correct the documents before the talk (no effort, and required regardless).* `SECURITY.md:57-59`, `USER_GUIDE.md`, `PROJECT_BIBLE.md` §4.1 TM-001, and the TM-001 row of `2026-08-10_threat-model-validation.md` all state or rely on "0/6 natural sentences". That number should be republished with the corpus it describes, and the residual restated: *isolated fragments and sentences that begin or end with the command words both fire; only strictly-interior occurrences are safe.*

---

### RT-02 — CRITICAL — A 4 KB deck defeats all four TM-018 render caps and stalls one slide for 92 seconds; 647 KB yields ~7.7 hours

**Vulnerability.** `measureComplexity` computes declared image area as `(w / 9525) * (h / 9525)` in `long long`, from `cx`/`cy` taken unclamped out of the deck via `toLongLong()`. Two independent ways to drive that to zero:

- **integer truncation** — any frame under 9525 EMU per side contributes exactly 0 (**already recorded as BUG-90(a)**);
- **signed 64-bit wrap** — choosing `cx = cy = 2³² × 9525` makes the product exactly `2⁶⁴ ≡ 0` (this variant is UB; see RT-03).

Because `decodeGuarded()` is called unconditionally before the `r.width() >= 1` guard (`slide_renderer.cpp:297`, **BUG-90(b)**) and there is no decoded-image cache, every frame triggers a full decode of the referenced media.

**Location.** `src/present/pre_render_worker.cpp:36`; `src/render/slide_renderer.cpp:297`.
**Prior art — stated plainly.** Both halves are recorded as **BUG-90** (SEV-2, "Open — post-talk"), found by the Phase 3 independent Security review on the same day. `PROJECT_BIBLE.md` §4.1 already marks TM-018 **FAIL**. **My contribution is the weaponised, measured end-to-end exploit and the confirmation that it scales linearly to the deck cap** — plus the observation in RT-11 that the *published gate artifact* still says TM-018 **Pass**.

**Severity: Critical** for an untrusted deck; **Not reachable** for the 2026-08-12 talk (Karl's worst slide uses ~1 % of the shape cap and every frame is far above sub-pixel), which is exactly why BUG-90 was deferred and I agree with that deferral.

**Proof of Concept.** Generator: `/tmp/redteam/gen_bomb.py`. Payload: 1,999 `<p:pic>` elements referencing one 30 Mpx PNG, each with `<a:ext cx="40926266982400" cy="40926266982400"/>`. Harness `/tmp/redteam/deck_probe.cpp` runs the exact shipped path — `DeckLoader::load` → `measureComplexity`/`exceedsCaps` → `SlideRenderer::render` — built `-O2` against the **release** `build/libpptv_core.a`.

```
############ decks/rt_render_bomb_control.pptx 4145 bytes      ← honest geometry
loaded slides=1 warnings=0
  slide 1: shapes=1999 runs=0 chars=0 imagePixels=1186958224  overCaps=YES
    PREVENTed (placeholder)
exit=0 elapsed=0.2s

############ decks/rt_render_bomb.pptx 4263 bytes              ← identical, extents tuned to wrap
loaded slides=1 warnings=0
  slide 1: shapes=1999 runs=0 chars=0 imagePixels=0  overCaps=no
    rendered 1600x900
exit=0 elapsed=92.3s
```

The two files differ **only** in the `cx`/`cy` attribute values and by 118 bytes. All four caps pass: `shapes=1999 ≤ 2000`, `runs=0 ≤ 5000`, `chars=0 ≤ 200000`, `imagePixels=0 ≤ 200000000`.

Linearity, then scale:

```
############ decks/rt_render_bomb_x3.pptx 8563 bytes
  slide 1..3: imagePixels=0  overCaps=no ; all rendered
exit=0 elapsed=278.6s                              ← 92.9 s/slide, linear

############ decks/rt_render_bomb_x300.pptx 647762 bytes
total uncompressed: 107.1 MB  (cap 1024 MB — accepted)
loaded slides=300 warnings=0
  slide 1: shapes=1999 runs=0 chars=0 imagePixels=0  overCaps=no
```

**647,762 bytes → 300 accepted slides × ~92.9 s ≈ 7.7 hours of pre-render.** ISOLATE holds (the UI thread never blocks) and the presenter can still drive the deck with the keyboard — but every slide is a grey placeholder, which BUG-21's own note calls "during a talk the same as losing it". The first slide alone shows *"Rendering slide 1…"* on the projector for 92 seconds.

**Exploitability: Trivial** — a 40-line Python script; **Difficult** to deliver, since the presenter must open a deck someone else supplied.
**Impact.** Denial of the presentation. No memory corruption, no data disclosure.

**Remediation.** As BUG-90 records, plus one addition it does not make — the decode cache, which is the cheapest of the three and fixes the amplification independently of the cap:

```cpp
// pre_render_worker.cpp — floor each dimension at one pixel and saturate.
constexpr long long kEmuPerPx = 9525;
const long long wpx = std::max<long long>(1, std::min(e.image.rect.cx, 1LL<<40) / kEmuPerPx);
const long long hpx = std::max<long long>(1, std::min(e.image.rect.cy, 1LL<<40) / kEmuPerPx);
c.imagePixels = std::min(c.imagePixels + wpx * hpx, 1LL << 62);   // no wrap possible

// slide_renderer.cpp — decode ONCE per media part per slide, and only if drawable.
if (r.width() < 1 || r.height() < 1) { break; }          // check BEFORE decoding
QHash<QByteArray, QImage> decodedCache;                   // keyed by mediaPart
```

With the decode cache alone, `rt_render_bomb.pptx` drops from 92.3 s to well under a second, because the cost is 1,999 decodes of the same bytes.

---

### RT-03 — HIGH — Two undefined-behaviour sites in the TM-018 cap itself, one of them the accumulator nobody has recorded

**Vulnerability.** Both the multiply and the add in `measureComplexity` are signed 64-bit overflow — undefined behaviour on attacker-controlled input, in the guard whose purpose is to reject that input.

**Location.** `src/present/pre_render_worker.cpp:36` (multiply) **and `:38` (accumulate)**.

**Severity: High.** UB on a live parse path. BUG-90 records the multiply (*"the `px > 0` guard runs AFTER the multiply"*). **Line 38 is not recorded anywhere** — neither BUG-90 nor `security-review-v1.md` mentions it, and `c.imagePixels < (1LL << 62)` guards the *accumulator*, never the addend.

**Proof of Concept.** UBSan output, verbatim, from `/tmp/redteam/deck_probe` (`-fsanitize=address,undefined`):

```
pre_render_worker.cpp:36:41: runtime error: signed integer overflow:
    4294967296 * 4294967296 cannot be represented in type 'long long'
    #0 pptv::measureComplexity(pptv::Slide const&) pre_render_worker.cpp:36
                                                    ← decks/rt_pixel_overflow.pptx

pre_render_worker.cpp:36:41: runtime error: signed integer overflow:
    484166511120985 * 484166511120985 cannot be represented in type 'long long'
pre_render_worker.cpp:38:27: runtime error: signed integer overflow:
    2219609700390740721 + 8880375467607446825 cannot be represented in type 'long long'
                                                    ← decks/rt_geometry_extremes.pptx
```

`rt_geometry_extremes.pptx` (2,306 bytes) also demonstrates the *easy* bypass — no tuning at all, just `cx="9223372036854775807"`:

```
  slide 1: shapes=3 runs=1 chars=1 imagePixels=-7346758905711364070  overCaps=no
```

A **negative** total is trivially "under" a 200,000,000 cap. An attacker does not need the precise `2³²` wrap of RT-02; any pair of implausibly large extents will do.

**Exploitability: Trivial. Impact.** Cap bypass (feeds RT-02) plus UB whose behaviour is compiler- and flag-dependent. Not weaponisable beyond DoS on the evidence I have.

**Remediation.** The clamp-and-saturate snippet in RT-02, plus the systemic fix: **range-check EMU at parse time**, where every one of these values enters:

```cpp
// deck_loader.cpp — parseXfrm(). A slide is ~1.2e7 EMU wide; 1e12 is a metre
// per pixel. Nothing legitimate is out here, and everything hostile is.
constexpr Emu kMaxEmu = 1'000'000'000'000LL;
static Emu emuAttr(const pugi::xml_node& n, const char* a) {
    return std::clamp(attrLocal(n, a).toLongLong(), -kMaxEmu, kMaxEmu);
}
```

That single change kills RT-03 and RT-05 at once and removes the class from every downstream consumer.

---

### RT-04 — HIGH — `sourceRectImpl`'s 64-bit widening fix is incomplete: the same expression overflows two lines below the comment explaining why it must not

**Vulnerability.** `slide_renderer.cpp:210-212` widens the srcRect sums to `qint64` for the **guard**, with a comment that reads:

> *"…two large values overflow signed int here — UNDEFINED BEHAVIOUR in the guard whose entire job is to reject bad input (adversarial review F5, reproduced under UBSan). A guard that invokes UB on the inputs it exists to catch is worse than no guard."*

Lines **217-218** then recompute the identical sums in plain `int`:

```cpp
const double w = imageSize.width()  * (1.0 - (sr.leftPerMille + sr.rightPerMille) / kFull);
const double h = imageSize.height() * (1.0 - (sr.topPerMille  + sr.bottomPerMille) / kFull);
```

The guard passes (`hSum = -4294967295` is not `>= 100000`), and then the computation invokes exactly the UB the guard was widened to avoid.

**Location.** `src/render/slide_renderer.cpp:217` and `:218`.
**Prior art:** none. Zero hits for `217`, `sourceRectImpl` overflow, or this expression in `security-review-v1.md`, `senior-engineer-review-v1.md`, `technical-user-review-v1.md`, or `BUGS.md`.

**Severity: High** — UB on a live path, and a *regression-shaped* defect: the codebase's own recorded lesson was applied to one line and not to the next.

**Proof of Concept.** `/tmp/redteam/decks/rt_srcrect_extremes.pptx` (2,262 bytes) — `<a:srcRect l="-2147483648" t="-2147483648" r="-2147483647" b="-2147483647"/>`:

```
slide_renderer.cpp:217:66: runtime error: signed integer overflow:
    -2147483648 + -2147483647 cannot be represented in type 'int'
    #0 pptv::slideSourceRect(QSize const&, pptv::SrcRect const&) slide_renderer.cpp:229
    #1 pptv::SlideRenderer::render(...) slide_renderer.cpp:306
slide_renderer.cpp:218:66: runtime error: signed integer overflow: (same)
```

Note the values are perfectly legal `ST_Percentage` integers as far as the parser is concerned — `parsePercentAttr` range-checks only the `%`-suffixed spelling (`deck_loader.cpp:517-527`); the bare-integer branch at `:529-533` calls `v.toInt(&ok)` and accepts any `int`.

**Exploitability: Trivial. Impact.** UB in the render path; downstream, the wrapped sum feeds a `QRectF` width, so a hostile deck can also steer which part of an image is drawn — a fidelity failure of the exact class the Manifesto forbids ("never a silent wrong render").

**Remediation.** Compute once, in 64-bit, and reuse:

```cpp
const qint64 hSum = static_cast<qint64>(sr.leftPerMille) + sr.rightPerMille;
const qint64 vSum = static_cast<qint64>(sr.topPerMille)  + sr.bottomPerMille;
if (hSum >= 100000 || vSum >= 100000) { return {}; }

const double x = imageSize.width()  * (sr.leftPerMille / kFull);
const double y = imageSize.height() * (sr.topPerMille  / kFull);
const double w = imageSize.width()  * (1.0 - hSum / kFull);   // reuse — no second add
const double h = imageSize.height() * (1.0 - vSum / kFull);
```

Better still, clamp at parse time: `parsePercentAttr` should reject any value outside `[-100000, 100000]` on **both** spellings, not just the `%` one.

---

### RT-05 — HIGH — `<a:rPr sz="nan">` reaches a float-to-int cast: undefined behaviour in the font-size clamp

**Vulnerability.** `run.fontSizePt = sz.toDouble() / 100.0` (`deck_loader.cpp:308`) uses the no-`ok` overload, so a failure is indistinguishable from a legitimate zero. `QString::toDouble()` **parses `"nan"`** — I verified this empirically rather than reasoning about it. The NaN then reaches:

```cpp
int clampFontPx(double px, double maxPx) {
    return static_cast<int>(std::clamp(px, 1.0, std::max(1.0, maxPx)));
}
```

`std::clamp` is `(v < lo) ? lo : (hi < v) ? hi : v`. Both comparisons are false for NaN, so NaN passes straight through, and `static_cast<int>(NaN)` is undefined behaviour. The function's own comment says it exists because "an unbounded declared size otherwise int-overflows the cast" — the magnitude case was fixed, the NaN case was not.

**Location.** `src/render/slide_renderer.cpp:31`; source at `src/loader/deck_loader.cpp:308` (and the identical pattern at `:245` `defRPrSizePt`, `:159-164` `parseXfrm`).
**Prior art:** none — zero hits for `clampFontPx` across all three concurrent Phase 3 reviews.

**Severity: High** — UB on the render path from a two-character edit to a deck.

**Proof of Concept.** `/tmp/redteam/decks/rt_fontsize_nan.pptx` (1,690 bytes), one shape, one run, `<a:rPr sz="nan"/>`:

```
slide_renderer.cpp:31:29: runtime error: nan is outside the range of
    representable values of type 'int'
    #0 drawTextBox(...)::$_0::operator()(pptv::TextRun const&) slide_renderer.cpp:111
    #1 pptv::SlideRenderer::render(...) slide_renderer.cpp:293
QFont::setPixelSize: Pixel size <= 0 (0)
```

Qt's own diagnostic on the following line confirms the cast produced garbage that then reached `QFont`. Tested variants: `"inf"`, `"-inf"`, `"1e400"` are all handled correctly (they saturate and clamp); **only `nan` is UB.**

**Exploitability: Trivial. Impact.** UB, then a font of undefined size. Not weaponisable on this evidence; the value that survives is compiler-dependent, and on ARM64 it lands as 0.

**Remediation.**

```cpp
// slide_renderer.cpp — NaN must be handled before the cast, because every
// comparison against NaN is false and std::clamp therefore passes it through.
int clampFontPx(double px, double maxPx) {
    if (!std::isfinite(px)) { return 1; }   // unreadable size -> the floor, never UB
    return static_cast<int>(std::clamp(px, 1.0, std::max(1.0, maxPx)));
}
```

And at the source, so the model never carries a non-finite size at all — this is the same treatment `parsePercentAttr` already gives percentages, and it should be applied to every numeric attribute:

```cpp
// deck_loader.cpp — parseRun()
bool ok = false;
const double pt = sz.toDouble(&ok) / 100.0;
if (ok && std::isfinite(pt) && pt > 0.0 && pt < 100000.0) {
    run.fontSizePt = pt;
}   // otherwise keep the inherited default — an unreadable size is not a size
```

---

### RT-06 — MEDIUM — Model-memory amplification of ~13,000×: the decompression caps bound the archive, not the model built from it

**Vulnerability.** `LoaderLimits` caps input bytes (200 MB archive, 128 MB per part, 1 GB total uncompressed) and element counts (5,000 shapes/slide, 2,000 paragraphs/box, 1,000 runs/paragraph). Nothing caps the **resident size of the `Presentation` model**. `<a:br/>` is 7 input bytes and becomes a whole `TextRun` — two `QString`s, a `double`, an `optional<Color>` and a heap allocation.

**Location.** `src/loader/deck_loader.cpp:357-363` (the `br` branch) and `src/loader/deck_loader.hpp:32-47`.

**Severity: Medium.** `DeckLoadWorker::start` has a `catch (...)` boundary (`deck_load_worker.cpp:73-83`), so the terminal outcome is a closed-vocabulary "could not be opened" rather than a crash — genuinely good design. The hazard is the swap storm before that point.

**Proof of Concept.** `/tmp/redteam/gen_amp.py`; measured with the **release** build:

| Payload | File size | Runs built | Peak RSS | Amplification |
|---|---|---|---|---|
| `rt_amplify_x1.pptx` | 24,406 B | 1,800,000 | **333 MB** | ~13,600× |
| `rt_amplify_x8.pptx` | 183,494 B | 14,400,000 | **2,348 MB** | ~12,800× |

Linear, at ~171 resident bytes per 7-byte `<a:br/>`. The `RenderCaps` correctly report `overCaps=YES` and `PREVENT` the render — but only **after** the model has been fully built, so the cap that is supposed to be the defence fires too late to be one.

Extrapolating the measured ratio to the enforced limits (128 MB per part × 8 parts = the 1 GB total cap, at the ~550:1 compression this XML achieves): a **~1.9 MB `.pptx` → ~25 GB of transient allocation.**

**Exploitability: Trivial. Impact.** Memory exhaustion and swap during load. On the target M3 Max the app recovers via the exception boundary; the machine's responsiveness in the interim is the real cost, mid-talk.

**Remediation.** Charge the model against a byte budget as it is built, rather than counting elements:

```cpp
// deck_loader.hpp
long long maxModelBytes = 512LL * 1024 * 1024;   // resident model ceiling

// deck_loader.cpp — parseTextBox(), threaded through as a running total
budget -= static_cast<long long>(sizeof(TextRun)) + run.text.size() * 2 + 32;
if (budget < 0) {
    LoadWarning w{index, QStringLiteral("text-cap"),
                  QStringLiteral("text body exceeds the memory budget; remainder skipped")};
    slide.warnings.push_back(std::move(w));
    break;
}
```

A `<a:br/>` run also does not need a heap `QString` at all — a `bool isBreak` on `TextRun` would remove the dominant term outright.

---

### RT-07 — MEDIUM — Duplicate-part resolution diverges from other OOXML readers; TM-007's test asserts determinism, which is not the property that matters

**Vulnerability.** When an archive carries two entries named `ppt/slides/slide1.xml`, `readPart`'s `zip_stat(za, name, 0, &st)` resolves to the **first** central-directory entry. Python's `zipfile` — the reader beneath `python-pptx` and most OOXML tooling — resolves the **last**, because `NameToInfo` is a dict populated in central-directory order.

**Location.** `src/loader/deck_loader.cpp:111-131`; claim at `PROJECT_BIBLE.md` §4.1 TM-007 (*"VERIFIED — asserts the answer is deterministic across runs"*) and the TM-007 row of `2026-08-10_threat-model-validation.md`.

**Severity: Medium.** The `SEC/TM-007` test proves determinism. Determinism is not a security property here; **agreement with the tool the presenter previews in** is. A deck can therefore render one thing in the preview tool and a different thing on the projector.

**Proof of Concept.** `/tmp/redteam/decks/rt_dup_slide.pptx` — two `slide1.xml` entries, the first containing `BENIGN FIRST ENTRY`, the second `HOSTILE SECOND ENTRY`:

```
$ python3 -c "import zipfile; ..."
entries named slide1.xml: 2
  central-dir entry 4 contains BENIGN
  central-dir entry 5 contains HOSTILE
python zipfile (NameToInfo, last-wins) picks: HOSTILE

$ ./deck_probe decks/rt_dup_slide.pptx
loaded slides=1 warnings=0
  slide 1: shapes=1 runs=1 chars=18 ...          ← 18 chars = "BENIGN FIRST ENTRY"
```

**Honest limit of this evidence:** I demonstrated divergence against Python's `zipfile`. I did **not** test Microsoft PowerPoint, Keynote, or macOS Quick Look, and I am not asserting which way any of them resolves. What I am asserting is that the two most obvious resolution rules are both in use in the wild, that powerpoint-voice takes one of them, and that the existing test cannot detect the mismatch because it asks the wrong question.

**Exploitability: Moderate** — requires supplying a deck and knowing the target's preview tool.
**Impact.** Content spoofing on a projector: benign in preview, attacker-chosen at showtime.

**Remediation.** Reject the ambiguity rather than resolving it. Walk the central directory once (the loop at `deck_loader.cpp:864-893` already does) and fail closed on any duplicate name:

```cpp
// deck_loader.cpp — inside the existing central-directory pass (~:866-893).
QSet<QString> seenNames;
for (zip_int64_t i = 0; i < n; ++i) {
    ...
    if (st.valid & ZIP_STAT_NAME) {
        // Two entries with one name is not a deck we can reason about: another
        // reader may resolve it the other way and show the presenter something
        // different from what lands on the projector (RT-07). Refuse it.
        if (!seenNames.insert(QString::fromUtf8(st.name)).second) {
            zip_close(za);
            return fail(LoadErrorKind::MalformedXml,
                        QStringLiteral("archive contains duplicate part names"));
        }
    }
}
```

and change the `SEC/TM-007` assertion from "deterministic" to "rejected". Then update the TM-007 row: `VERIFIED` on a test that measures the wrong property should not read as `VERIFIED`.

---

### RT-08 — MEDIUM — A 20 MB `<a:latin typeface="…">` is accepted verbatim into `QFont::setFamily`

**Vulnerability.** `run.fontFamily = attrLocal(latin, "typeface")` (`deck_loader.cpp:314`) has **no length cap**, while the run's *text* immediately above it is capped at `maxRunTextChars` (100,000). The unbounded string is stored per run and passed to `QFont::setFamily` (`slide_renderer.cpp:109`), which drives font-database matching.

**Location.** `src/loader/deck_loader.cpp:314`; `src/render/slide_renderer.cpp:108-109`.

**Severity: Medium**, and I am rating it down deliberately: I could not turn it into anything worse than allocation. `/tmp/redteam/decks/rt_typeface_20mb.pptx` (22,154 bytes, a 20 MB family name) loaded and rendered in 0.6 s at 363 MB peak RSS under ASan. Qt handled it without incident.

**Impact.** Memory amplification (~950× here) and a font-matching path fed an attacker-sized string. Combined with the per-slide caps, a deck could carry 5,000 such runs.

**Remediation.** One line, alongside the cap that already exists for text:

```cpp
// deck_loader.cpp — a font family name is never longer than this.
run.fontFamily = attrLocal(latin, "typeface").left(128);
```

---

### RT-09 — LOW — `SECURITY.md`'s drag-and-drop claim overstates what `localDeckPathFrom` does

**Vulnerability.** `SECURITY.md:13` states the drop handler *"explicitly refuses remote URLs (`localDeckDropPathFrom`, `src/ui/start_view.cpp`) so a dropped file cannot become the first network path by accident."* Two inaccuracies: the function is named `localDeckPathFrom`, and it refuses **non-`file:` schemes**, not remote content. `QUrl::isLocalFile()` is true for any `file:` URL, including one whose path resolves onto an SMB/AFP mount under `/Volumes/`.

**Location.** `src/ui/start_view.cpp:17-27`; claim at `SECURITY.md:13`.

**Severity: Low.** The *effect* — no HTTP fetch is ever issued — is real, because there is no network code to issue one. It is the *reason* that is misstated, and a future maintainer reading that line may believe a check exists that does not.

**Remediation.** Restate the claim accurately, or make it true:

```cpp
// start_view.cpp — a network volume is still a network path, whatever the scheme says.
for (const QUrl& url : mime->urls()) {
    if (!url.isLocalFile()) { continue; }
    const QString p = url.toLocalFile();
    if (p.startsWith(QLatin1String("/Volumes/")) || p.startsWith(QLatin1String("//"))) {
        continue;   // a mounted share is not local storage
    }
    return p;
}
```

---

### RT-10 — LOW — `LoadWarning::detail` carries attacker-controlled deck bytes and is built on every load, but nothing consumes it

**Vulnerability.** `w.detail = uri.isEmpty() ? name : uri;` (`deck_loader.cpp:758`) puts the deck's `<a:graphicData uri="…">` — arbitrary attacker text — into the model. Other `detail` values embed the resolved part path and relationship id (`:1014`, `:1021`).

**Location.** `src/loader/deck_loader.cpp:758`, `:1014`, `:1021`.

**Severity: Low — this is a latent hazard, not a live one, and I want to be precise about why.** I traced every consumer:

```
grep -rn "warnings\|detail" src/   →  only deck_loader.cpp and slide_model.hpp
```

Nothing in `src/` reads `warnings` or `detail`. The **rendered** label `e.unsupported.type` is a genuinely closed vocabulary (`"table"`/`"chart"`/`"smartArt"`/`"graphicFrame"`/`"cxnSp"`), chosen by `uri.contains(...)` — the URI selects a label, it never becomes one. `notice.cpp` is exactly what TM-012 claims: every string from an id plus integers. **TM-012 and TM-013 hold.**

The finding is that the *material* for a disclosure is assembled, carried through the model, and sits one `setStatusText()` call away from a projector — and the only thing preventing it is that the load-report view described in `PROJECT_BIBLE.md` was never built. `tools/render_preview.cpp:44-46` already prints `warn.detail` to stdout; it is a dev tool and I confirmed it is **not** in the bundle (`build/powerpoint_voice.app/Contents/MacOS/` contains only `powerpoint_voice`).

**Remediation.** Make the invariant structural instead of accidental, so the load-report feature cannot silently break it:

```cpp
// slide_model.hpp — detail becomes a closed enum, like NoticeId already is.
enum class WarningDetail { None, ShapeCap, GroupDepthCap, UnreadableCrop,
                           UnreadableOpacity, MissingSlidePart, NoRelationship,
                           UnsupportedBackground, NoSlideSize };
struct LoadWarning { int slideIndex = 0; QString elementType; WarningDetail detail{}; };
```

---

### RT-11 — MEDIUM — The Phase 3 gate artifact contradicts the Bible on its own highest-severity threat

**Vulnerability.** `PROJECT_BIBLE.md` §4.1 now records **TM-018: FAIL (BUG-90)** and **TM-021: PARTIAL, "an independent reviewer found a fresh signed-overflow UB in this path in ~1 hour"**. The document that §4.1 cites as its evidence — `docs/test-results/2026-08-10_threat-model-validation.md` — still records **TM-018: Pass** and summarises **"Fail: 0"**. `SECURITY.md:41-43` likewise still asserts that *"four render caps … reject a pathological slide before it is painted"*, which RT-02 disproves with a 4 KB file.

**Location.** `docs/test-results/2026-08-10_threat-model-validation.md` (TM-018 row, TM-007 row, Summary table); `SECURITY.md:41-43, 57-59`.

**Severity: Medium**, and it is a governance finding rather than a technical one — but it is the gate artifact, and the Phase 3→4 gate reads it. Three of its rows are now known wrong: TM-018 (`Pass` → `Fail`, per BUG-90), TM-001 (`0/6 natural sentences` → 62/102, per RT-01), TM-007 (`Pass` on a test that measures determinism rather than agreement, per RT-07). Its summary line *"Fail: 0"* is the number a gate reviewer will read.

**Remediation.** Regenerate the validation report from the current Bible, and add a CI check that fails if a TM row's status differs between `PROJECT_BIBLE.md` §4.1 and the newest `*_threat-model-validation.md`. The project already has `scripts/lint-review-manifest.sh`; this is the same shape of control.

---

## Phase 3 — Attack Chains

### Chain A — The heckler (or the polite audience member). Confidence: **HIGH**

1. Presenter says *"pause presentation"* before Q&A. `RecognizerController` → `Paused`. Banner: *"Paused — voice control is off."* The microphone remains live by design (`recognizer_controller.cpp:41-44`); BUG-88 already records that the banner's wording is false.
2. Anyone in the room says *"That answers my question, so please continue the presentation."* Vosk emits `[unk] continue the presentation`; `matchCommand` strips the single leading `[unk]`; `ContinuePresentation` dispatches. **Measured 46/48 across 3 voices (RT-01).**
3. The session is now Active. Voice navigation is live again, and the presenter has no reliable signal — BUG-92 establishes that one arrow keypress replaces the pause banner with *"Slide N"*.
4. From there, TM-001's already-accepted residual applies at full strength: any near-miss fragment moves the deck.

No tooling, no preparation, ordinary English. The only step that is not certain is step 2, and it is 95.8 % reliable.

### Chain B — The supplied deck. Confidence: **HIGH** (technically) / **LOW** (as a delivery path today)

1. Attacker sends a conference deck: `rt_render_bomb_x300.pptx`, 647,762 bytes, opens in any ZIP tool, contains 300 ordinary-looking slides.
2. `DeckLoader::load` accepts it — 107 MB uncompressed, well under the 1 GB cap; all 300 slides report `imagePixels=0, overCaps=no` (RT-02/RT-03).
3. `PreRenderWorker` begins. Slide 1 takes 92.3 s; the projector shows *"Rendering slide 1…"*. The full deck is ~7.7 hours of pre-render.
4. The presenter can still drive the deck by keyboard, and every slide is a grey placeholder for the duration.

Confidence in the *mechanism* is High — I measured every step. Confidence in the *delivery* is Low for the 2026-08-12 talk, because the presenter opens only his own deck, which is exactly why BUG-90 was deferred and I concur.

### Chain C — Supply chain. Confidence: **LOW**

`libzip`/`pugixml` come from Homebrew and are linked dynamically at build time, then re-staged into the bundle by `make-test-build.sh` (which fails if any binary still references `/opt/homebrew` — verified). Vosk, miniaudio and the model are vendored and SHA-256 pinned, and I re-read `PROVENANCE.md` against `sbom.json` without finding a discrepancy. The one known CVE in a pinned version — **CVE-2026-32837**, heap OOB read in miniaudio's `ma_dr_wav` BEXT parsing — is compiled out by `MA_NO_DECODING` and the project verified zero `dr_wav` symbols in the shipped binary. I did not re-verify the symbol table myself; the reasoning is sound and the mitigation is one `#define` away from reverting, which `PROVENANCE.md` says in bold.

The realistic blast radius is **RT-02/RT-03 in reverse**: because there is no hardened runtime and no library validation (TM-022/023), anyone with write access to the installed bundle owns the process. That is Phase 4 work and is already recorded.

---

## Remediation Priority List

| # | Finding | Fix | Effort | Before the talk? |
|---|---|---|---|---|
| 1 | RT-01 | Refuse `ContinuePresentation` when an `[unk]` edge was stripped (7-line diff, `command_matcher.cpp`) | Quick fix | **Yes** |
| 2 | RT-01 | Republish the false-trigger figure with the corpus shape it describes; correct `SECURITY.md`, `USER_GUIDE.md`, Bible §4.1 TM-001, validation report | Quick fix | **Yes** |
| 3 | RT-11 | Regenerate `2026-08-10_threat-model-validation.md` from Bible §4.1 (TM-018 `Fail`, TM-001, TM-007); fix the `Fail: 0` summary | Quick fix | **Yes** — it is the gate artifact |
| 4 | RT-02 | Cache decoded images per media part per slide; move `decodeGuarded` behind the `r.width() >= 1` guard | Quick fix | No (BUG-90 deferred) |
| 5 | RT-03/RT-04/RT-05 | Clamp EMU in `parseXfrm`; reuse the 64-bit sums in `sourceRectImpl`; `isfinite` guard in `clampFontPx` | Quick fix | No |
| 6 | RT-02/RT-03 | Floor each frame dimension at 1 px and saturate the accumulator in `measureComplexity` | Moderate | No |
| 7 | — | **Add ASan+UBSan to CI.** Every one of RT-03/04/05 is a one-run find; none of them survives a sanitizer job. `PROJECT_BIBLE.md` already corrected the false "in CI" claim — closing it costs one workflow file | Moderate | No |
| 8 | RT-06 | Byte budget for the model; `bool isBreak` on `TextRun` instead of a heap `QString` | Moderate | No |
| 9 | RT-07 | Reject duplicate part names in the central-directory pass; change `SEC/TM-007` to assert rejection | Quick fix | No |
| 10 | RT-08/RT-10 | `.left(128)` on `typeface`; turn `LoadWarning::detail` into an enum before the load-report view is built | Quick fix | No |

**Highest value per line changed: #1, #3, and #7.** #1 is seven lines and closes the only Critical that is reachable at the talk. #3 costs nothing but honesty. #7 would have caught RT-03, RT-04 and RT-05 automatically and will catch the next three.

---

## Positive Findings — what to preserve

These are not consolation prizes. I tried to break each of them and could not.

1. **Containment is real and structural.** No network code, no write path, no `QProcess`, no `getenv`, no `QSettings`, no IPC. I verified by exhaustive grep, not by reading the claim. **TM-005 and TM-011 hold as STRUCTURAL.**
2. **TM-004 zip-slip: STRUCTURAL, confirmed.** `/tmp/redteam/decks/rt_zipslip_hard.pptx` carries a traversing entry name (`../../../../tmp/redteam/ZIPSLIP_PROBE.txt`), an absolute entry name (`/tmp/redteam/ZIPSLIP_ABS.txt`), a **UNIX symlink entry** (`0o120777`) named `ppt/media/image1.png` pointing at `/etc/passwd`, and three traversing relationship Targets including `/etc/passwd` and `../../../../tmp/...`. Result: the deck loads, the traversing references resolve to zero bytes, and **no file appears anywhere on disk**. `resolveTarget` collapses `..` without ever underflowing the segment list, and `readPart` resolves names *inside* the archive. There is no extraction step to attack. This claim is correct and stronger than the report states — the symlink case is not in the existing fixture.
3. **TM-010 XXE: STRUCTURAL, confirmed.** `rt_xxe_hard.pptx` carries a `<!DOCTYPE>` with a general entity (`file:///etc/passwd`), a **parameter** entity, an external DTD reference, and a nested entity; `rt_xxe_rels.pptx` puts the entity in a relationship `Target` instead of slide text. Neither expands: the model records `chars=16`, the literal length of `"&xxe; / &nested;"`. pugixml's default `parse_default` mask excludes `parse_doctype` and never resolves external entities. Correct.
4. **TM-016 embedded fonts: STRUCTURAL, confirmed.** `rt_font_embed.pptx` carries a malformed `ppt/fonts/font1.fntdata` part and a run naming an uninstalled family. No `QRawFont`, no `QFontDatabase::addApplicationFont`, no font-file read exists anywhere in `src/`. The part is inert. Correct.
5. **TM-012 transcript: STRUCTURAL, confirmed.** `notice.cpp` is exactly what it claims — every string from a `NoticeId` plus integers. Heard text goes decoder → gate and is never stored, logged, or rendered (see RT-10 for the one latent caveat).
6. **The image allow-list is the right shape.** Magic bytes checked *before any decoder is constructed* (`slide_renderer.cpp:48-56`), excluding the CVE-prone TIFF/WebP/GIF codecs, with the reasoning about `QImageReader::format()` probing plugins off the GUI thread recorded in the comment. This is the pattern to copy elsewhere.
7. **The exception boundaries are correct and well-reasoned.** All three of them — `DeckLoadWorker::start`, `PreRenderWorker::renderOne`/`start`, and `VoicePipeline::onSamples` (the real-time audio thread, BUG-83). The `catch (...)` on a C-ABI callback frame is the right call and the comment explains why.
8. **The size caps are enforced from the central directory before any part is read, unsigned.** `rt_zip64_lie.pptx` (a forged `0xFFFFFFFF` uncompressed size) is rejected with `LoadErrorKind::PartTooLarge` before a byte is decompressed. **TM-014 holds against my payloads.**
9. **Grammar enforcement fails closed** — static-graph model refused, every grammar word round-tripped through `vosk_model_find_word`, and the `[unk]` escape hatch verified against the *emitted JSON* rather than the input phrases. That last one is a genuinely sophisticated check.
10. **No secrets, anywhere.** Independent pattern sweep across `src/ tools/ scripts/ cmake/ .github/`: zero matches.
11. **The candour is itself a control.** `SECURITY.md` withdraws a previously-published false claim in bold. `PROJECT_BIBLE.md` §4.1 marks its own highest-severity threat **FAIL** and annotates TM-021 with *"This row said 'in CI' until 2026-08-10; that was false"*. I have reviewed codebases that would have buried both. RT-01 is offered in that spirit: the number is wrong, and the project has already shown it knows what to do with a wrong number.

---

## Fuzzing Campaign (TM-021)

`PROJECT_BIBLE.md` §4.1 records TM-021 as **PARTIAL — no fuzzing campaign**, and rates it the one High-risk accepted item. So I ran one.

**Harness:** `/tmp/redteam/fuzz.py`. Seeds are the project's own 44 fixtures. Each iteration rebuilds a **valid ZIP** whose XML/rels parts have been mutated — bit flips, random bytes, chunk deletion, chunk duplication (to drive nesting), tag injection (`<a:br/>`, `<p:grpSp>`, `<a:ext cx="9223372036854775807">`, `<a:rPr sz="nan">`, …), and splicing of 23 interesting numeric literals (`nan`, `inf`, `INT_MIN`, `2^63-1`, `1e400`, `1e30%`, …). Each case runs through the full `load → measureComplexity → render` path under `-fsanitize=address,undefined`.

| Campaign | Seeds | Iterations | ASan reports | UBSan reports | >25 s hangs |
|---|---|---|---|---|---|
| Clean | project fixtures only | 3,000 | **0** | **0** | 0 |
| Seeded | fixtures + my 25 payloads | 4,000 | **0** | 31 | 12 |

**Distinct UBSan sites found — all five, from a corpus of ~7,000 mutations:**

```
src/present/pre_render_worker.cpp:36:41    signed integer overflow (multiply)   RT-03 / BUG-90
src/present/pre_render_worker.cpp:38:27    signed integer overflow (accumulate) RT-03  ← unrecorded
src/render/slide_renderer.cpp:217:66       signed integer overflow (int sum)    RT-04  ← unrecorded
src/render/slide_renderer.cpp:218:66       signed integer overflow (int sum)    RT-04  ← unrecorded
src/render/slide_renderer.cpp:31:29        NaN → int cast                       RT-05  ← unrecorded
```

**The most important line in this section is the one that says zero.** Across roughly 7,000 mutated archives, **AddressSanitizer produced not one report** — no heap overflow, no use-after-free, no double free, no stack overflow. Every finding is integer/float undefined behaviour in *guards*, not memory corruption in the *parser*.

That is not a proof, and I will not dress it up as one: this is short-run, non-coverage-guided mutation fuzzing over a fixed seed corpus, not libFuzzer with a persistent harness and a corpus grown over CPU-days. But it is meaningfully more evidence than the project had this morning, and it points the same way its own construction history does — the four historical defects (BUG-52/56/57/79) were all found and fixed, and the iterative-descent decision removed the obvious stack-exhaustion class.

**My honest read of TM-021: the residual is lower than "High" suggests, and the right next control is not more fuzzing — it is `-fsanitize=address,undefined` in CI.** All five sites above are single-run finds; none would survive one sanitizer job. The project's cheapest unclaimed control is a workflow file.

---

## Automated Tooling Gaps

What a scanner would have missed here, and why. This matters because `2026-08-10_semgrep_pass.json` is clean and that cleanliness is doing more work in the gate than it should.

| Finding | Why SAST/DAST/SCA misses it |
|---|---|
| **RT-01** | The defect is *linguistic*. `matchCommand` is correct C++ — clean control flow, no unsafe call, no taint sink. The bug is that "one `[unk]` means a sentence, not a command" is a claim about English and about Vosk's tokenisation. It is findable **only** by synthesising audio and listening to what comes out. No static tool models a speech decoder. |
| **RT-02** | Every cap is present and syntactically correct. The bug is that the arithmetic *inside* the cap can be driven to zero. A scanner sees a bounds check and moves on. Finding it needs a payload built to make the check lie. |
| **RT-03/RT-04/RT-05** | UBSan finds all three **in one run** — but only against an input that reaches them, and only in a build that has UBSan on. This project has such a build (`build-asan/`) and does not run it in CI. The gap is not tooling capability; it is that the tool is never pointed at the code. |
| **RT-04 specifically** | The *hardest* class for any tool: an **incomplete fix**. Lines 210-212 are correct and carry a comment explaining the danger. Lines 217-218 recompute the same expression unsafely. A diff review sees the fix; a scanner sees a widened guard; only re-deriving the data flow finds the second site. |
| **RT-06** | Every count is bounded. No tool models the *ratio* between input bytes and resident bytes. Needs measurement. |
| **RT-07** | Requires knowing how a *different* implementation resolves the same ambiguity. That is comparative behaviour, not a code property. |
| **RT-10** | The dangerous flow does not exist yet — it is a source with no sink. Taint analysis correctly reports nothing. The risk is that a future feature completes the path, which is a design observation, not a finding a tool can make. |
| **RT-11** | Two Markdown files disagreeing. Only a doc-consistency linter would catch it, and the project does not have one for this pair — though it has exactly that shape of control elsewhere (`lint-review-manifest.sh`). |

The pattern: **every finding above the Low line came from running something, not from reading something.** A clean Semgrep run on this codebase means Semgrep found nothing, and that is all it means.

---

## Overall Security Rating

### **Do Not Deploy** — for any deck the presenter did not author.

**Blockers for wider distribution:**

1. **RT-02/RT-03** — the TM-018 render bomb is live for untrusted decks and reproduces from a 4 KB file. `PROJECT_BIBLE.md` already marks TM-018 **FAIL**; that is the correct status and it is a distribution blocker.
2. **RT-04/RT-05** — undefined behaviour on the render path from two-character deck edits. UB is not a severity you can bound by argument.
3. **RT-06** — a ~1.9 MB file drives ~25 GB of transient allocation.
4. **TM-021 stays open.** My campaign found no memory-safety defect and that is real evidence, but it is not a fuzzing programme, and the acceptance in `2026-08-10_threat-model-validation.md` explicitly scopes itself to "a file he authored himself."

### For the 2026-08-12 talk specifically: **Deploy with Conditions.**

RT-02, RT-03, RT-04, RT-05, RT-06, RT-07 and RT-08 all require a deck the presenter did not author. He is presenting his own. That exposure is genuinely near zero and the existing acceptance is sound reasoning, not hand-waving.

**RT-01 is the exception, and it is the condition.** It needs no deck, no file, no tooling — only a person in the room speaking ordinary English. It is reachable at this talk, it defeats the specific mitigation the accepted TM-001/TM-002 risk names, and the evidence the acceptance rests on is wrong by a factor I measured at 62/102 against a published 0/6.

**Conditions, in order:**

1. **Apply the seven-line `strippedUnk` guard in `matchCommand`** (RT-01, remediation 1). Smallest possible change; takes the un-pause family from 46/48 to 0/48; cannot regress a true single-utterance command.
2. **Correct the "0/6 natural sentences" figure** wherever it appears — `SECURITY.md`, `USER_GUIDE.md`, `PROJECT_BIBLE.md` §4.1, `2026-08-10_threat-model-validation.md` — and restate the residual as: *isolated fragments fire; sentences that begin or end with the command words fire; only strictly-interior occurrences are safe.*
3. **Regenerate the threat-model validation report** so the gate artifact stops saying `Fail: 0` (RT-11).
4. **Do not open a deck from anyone else** on the talk machine until items 4-6 of the priority list land. Record this as an operating condition, not a preference.
5. **Re-record the TM-001 acceptance** against the corrected number. The existing signature was given on evidence that is now known to be wrong; that is a fact about the evidence, not about the decision, and the decision may well be the same. It should be made again anyway.

---

### A closing note on method

Everything above that is stated as measured, I measured — on this machine, today, with harnesses built against this repository's own libraries. Where I am extrapolating (the 300-slide render time, the 25 GB allocation ceiling) I have shown the arithmetic and the measurement it rests on. Where prior work found something first (**BUG-90** for the TM-018 cap bypass, **BUG-88** for the live microphone during pause, **BUG-92** for the non-durable banner) I have said so in the finding rather than at the end. And where a claim survived a genuine attempt to break it — TM-004, TM-005, TM-010, TM-012, TM-016, TM-014 — I have said that too, with the payload that failed to break it.

The most useful thing I can leave behind is not the findings list. It is this: **five of the eleven findings, including both Criticals, were invisible to every static control this project runs, and visible within minutes to something that actually executed the code with hostile input.** The sanitizer builds already exist. The acoustic harness is forty lines. Both belong in CI.

**Reproduction:** payload generators (`gen_attacks.py`, `gen_bomb.py`, `gen_amp.py`), harnesses (`deck_probe.cpp`, `voice_probe.cpp`), the fuzzer (`fuzz.py`), 25 attack decks, 102 audio clips, and every log referenced above are in `/tmp/redteam/`. Nothing in this repository was modified except this file.
