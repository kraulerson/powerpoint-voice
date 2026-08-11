# Security Review — powerpoint-voice

**Reviewer:** Senior Vice President, IT Security (independent Phase 3 review, AI agent arm)
**Date:** 2026-08-10
**Scope:** Full repository, read-only. Source tree ~4,758 LOC across `src/`, plus build, CI, vendored dependencies, and all Phase 3 security artifacts of record.
**Track:** full · **Deployment:** organizational · **Artifact of record:** this file (`security-review-v1.md`), gate condition for Phase 3→4.
**Method:** Every claim in `SECURITY.md`, `third_party/PROVENANCE.md`, and `docs/test-results/2026-08-10_threat-model-validation.md` was tested against the code rather than accepted. No project code was executed. No file other than this one was modified. The Confidential deck at `../Solo Orchestrator - FirstService IT Summit.pptx` was not opened.

---

## 1. Security Executive Summary

This is an unusually careful piece of security engineering for a solo project, and its core containment claims are real: I independently verified that there is no network code, no write-path to disk, no process execution, and no logging anywhere in `src/`, and that recognised speech is consumed by a command gate and discarded without ever being stored, logged, or rendered. The vendored dependency hashes match `PROVENANCE.md` byte-for-byte, and the miniaudio CVE unreachability argument holds at source level. That said, the Phase 3 threat-model validation that this gate depends on **overstates its own results in ways I was able to falsify from the code**: TM-018 (the report's self-declared highest-severity live-presentation threat) is marked "Pass" but its image cap measures declared *frame* geometry through a truncating integer division and is trivially evaded; the same expression contains an attacker-reachable signed-integer overflow; TM-014 is marked "Pass" while a 300x decompression-amplification bypass found by the project's own testers sits deferred and unfixed; and TM-011/TM-012 are marked "Pass" despite a reproduced, still-live channel that writes deck-derived bytes into the macOS unified log. Underneath that, the assurance apparatus is weaker than the paperwork implies: **no dependency vulnerability scan has ever run** (CI's OSV step is disabled by a missing manifest and defers to a Phase 3 Snyk scan that is recorded as SKIP/unauthenticated), the SBOM carries `pkg:generic` purls and zero CPEs so it is not machine-matchable by any scanner, the only real SAST is Semgrep whose C++ blindness the project's own positive-control test already proved, and the sanitizer coverage claimed "in CI" for TM-021 does not exist in CI at all. Finally, the eight accepted risks are scoped to "the presenter's own machine and that talk," and **nothing whatsoever enforces that scope** — no expiry, no build flag, no runtime check, no distribution control — which matters because Phase 4 *is* distribution, and the act of entering it invalidates the premise the acceptances were granted on. For the narrow scope already exercised, the residual risk is genuinely low and the owner's acceptance was reasonable; for the organizational, distributed posture Phase 4 implies, this is not ready, and the gate artifact must be corrected before it can be relied on as evidence.

---

## 2. Threat Model Summary — Top 5 Threats

| # | Threat | Why it is top-5 | Severity |
|---|---|---|---|
| **T1** | **Hostile `.pptx` achieves memory-safety exploitation in the OOXML parser.** ~1,150 lines of hand-written C++ walking attacker-controlled ZIP + XML, four real memory-safety defects already found during construction, **zero fuzzing**, no sanitizers in CI, no compiler hardening flags, and no hardened runtime to contain a successful exploit. I found a fresh signed-overflow UB in the same input path (SEC-02) in under an hour of reading — which is the empirical answer to whether the surface is clean. | **High** |
| **T2** | **Live-presentation denial via a render bomb that the TM-018 caps do not catch.** The "declared image pixels" cap reads frame geometry through `(w/9525)*(h/9525)`; sub-pixel frames contribute zero, and the renderer decodes every picture element unconditionally with no decoded-image cache. Reproduces the project's own measured 309 s stall while the cap reads 0. Marked "Pass". | **High** |
| **T3** | **Deck content leaves the process into a persisted OS artifact.** libpng's default error handler writes bytes lifted verbatim from media parts to stderr; a Finder-launched `.app` has stderr captured by the macOS unified log. Reproduced by a prior tester, never assigned a bug ID, never fixed, and the two threats that cover it are marked "Pass" using an inspection method (`grep` for write calls in `src/`) that cannot see it. Applies to the *Confidential* deck, not only to hostile ones. | **High** |
| **T4** | **Local code injection into a process holding the deck and the live microphone.** Ad-hoc signature, no hardened runtime, no library validation, no entitlements, no notarization. Any local process running as the user can `DYLD_INSERT_LIBRARIES` into the app and thereby add the network egress that "there is no network code" is meant to preclude, and inherit the microphone TCC grant. The three headline properties are enforced by *source-code absence*, which is not a runtime control. | **High** |
| **T5** | **Unknown-CVE exposure across the entire dependency set.** Qt 6.11.1, libzip 1.11.4, pugixml 1.16, Vosk 0.3.44 have never been checked against any vulnerability database by any tool, at any point. The one known CVE in the tree was found by hand. The SBOM cannot be used to close this gap as written. | **High** |

---

## 3. Category Assessments

### 3.1 Attack Surface Analysis

**Finding.** The entry points are exactly three, and the inventory in `SECURITY.md` is accurate and complete:
1. **A `.pptx` file**, supplied by (a) `Cmd+O` file dialog, (b) drag-and-drop (`src/ui/start_view.cpp:17`), (c) `argv[1]` (`src/main.cpp`, positional `deck` argument).
2. **The default system microphone** (`src/audio/miniaudio_capture.cpp`, `cfg.capture.pDeviceID = nullptr`).
3. **The bundled Vosk model directory**, resolved at `src/command/vosk_recognizer.cpp:119` from three candidate paths, the last of which is a compile-time absolute path into the developer's build tree (`PPTV_VOSK_MODEL_DIR`).

Network exposure is **nil at source level** — I grepped for `QNetwork*`, `QTcp*`, `QUdp*`, `QSsl*`, `QLocalSocket`, `QLocalServer`, `socket(`, `curl`, `QDesktopServices::openUrl`, and raw URL literals across `src/`: **zero hits**. There is no listening socket, no client, no IPC, no child process (`QProcess`, `system`, `exec*`, `popen`, `fork`, `posix_spawn`, `NSTask`: **zero hits**), and no update channel. Third-party trust boundaries are crossed only in-process: libzip and pugixml consume attacker bytes directly; Qt's PNG/JPEG codecs consume attacker bytes behind a magic-byte gate; Vosk consumes microphone PCM.

Two caveats on the minimality claim. First, the drop handler (`localDeckPathFrom`) accepts **any** local file URL with no extension or content pre-filter, so any dropped file reaches the ZIP/XML parser. Second, `url.isLocalFile()` is satisfied by a `file://` path on an SMB/NFS/autofs mount, so "no network" is a statement about the application's *code*, not about its *I/O*; a deck opened from a network share is fetched over the network by the kernel on the app's behalf.

**Threat Model.** Actor: anyone who can put a file in front of the presenter (email, USB, shared drive) or stand in the room. There is no remote actor because there is no remote surface — this is correct and structurally so.

**Severity:** Informational (surface is genuinely minimal). **Exploitability:** N/A.

**Remediation.** Filter drops to `.pptx` before invoking the loader — defence in depth, not a fix. Replace the `PPTV_VOSK_MODEL_DIR` build-tree fallback with a hard failure in release builds so a shipped binary cannot be steered to a model directory outside the bundle.

---

### 3.2 Authentication and Authorization

**Finding.** There is no authentication and no authorization, and for a single-user local presentation tool with no network surface and no multi-user state, **that is the correct design**. There are no credentials, no sessions, no tokens, no roles.

The one place an authorization-shaped boundary exists is the voice command gate, and it is worth naming precisely: **every human within earshot is an unauthenticated operator with full command authority.** The project models this honestly as TM-001/TM-002 and does not pretend otherwise — `SECURITY.md` explicitly withdraws an earlier false claim that the decoder could emit only the five commands, and records the measured result (60/102 isolated near-miss fragments match; 0/6 natural sentences). That withdrawal is the single strongest signal of good faith in this repository.

The available controls are real but partial: `RecognizerController` (`src/command/recognizer_controller.cpp`) gates navigation while Paused, and the keyboard path is independent of the speech engine by construction (`key_translator.cpp`), so voice can be denied but the deck cannot.

**Threat Model.** Actor: a heckler, or an unlucky audience member reading a slide aloud. Impact: unauthorized slide navigation in front of an audience. Not confidentiality-affecting (the deck is already projected), but reputationally live.

**Severity:** Medium — correctly identified, honestly measured, accepted with a usable mitigation. **Exploitability:** Trivial (speak), but bounded: the matcher requires the whole utterance, which is why natural speech scored 0/6.

**Remediation.** None required for the accepted scope. If this ever ships beyond one presenter, add a push-to-talk modifier — the only control that actually removes audience command authority rather than reducing its probability.

---

### 3.3 Input Validation and Injection

**Finding.** Classical injection is **absent by construction**: no SQL, no HTML/DOM, no shell, no `eval`, no template engine, no deserialization of untrusted formats beyond XML. Output encoding is not applicable because there is no output channel other than a raster. This is a real and verified negative.

Path traversal is **contained, and the TM-004 "structural" claim holds.** `resolveTarget` (`deck_loader.cpp:90`) normalises `../` segments, but the decisive property is that the result is only ever passed to `zip_stat`/`zip_fopen` as an *archive entry name* — nothing is extracted to the filesystem anywhere in the loader. A traversing target resolves to a nonexistent archive entry and yields zero bytes. Verified by reading every call site of `readPart`.

XXE and entity expansion are **contained**: pugixml does not resolve external entities or expand DTDs by default, and `SEC/TM-010` and `SEC/TM-010/017` in `tests/test_deck_loader.cpp` exercise a real `file:///etc/passwd` payload and a nine-level billion-laughs. Both are genuine attack payloads, both assert the deck loads first. Good work.

The caps are largely well-built — enforced from the central directory before any part is read, compared **unsigned** so a ZIP64 declared size cannot wrap negative (`deck_loader.cpp:875`), with the part name copied before `zip_close` to avoid the use-after-free that previously printed freed heap into a projector-facing dialog. XML descent is genuinely iterative (`descendantLocal`, `deck_loader.cpp:43`) with an explicit heap stack, and group nesting is capped at 32.

**But three input-validation defects survive, and two of them falsify "Pass" ratings in the gate artifact.**

---

#### **SEC-01 — TM-018 render-bomb cap measures the wrong quantity and is trivially evaded (HIGH)**

`src/present/pre_render_worker.cpp:33-39`:

```cpp
const long long w = e.image.rect.cx > 0 ? e.image.rect.cx : 0;
const long long h = e.image.rect.cy > 0 ? e.image.rect.cy : 0;
// EMU -> approximate pixels at 96 dpi: 914400 EMU per inch.
const long long px = (w / 9525) * (h / 9525);
if (px > 0 && c.imagePixels < (1LL << 62)) {
    c.imagePixels += px;
}
```

Two independent failures:

1. **Truncating integer division.** Any picture frame smaller than 9,525 EMU (≈1 px) in *either* dimension contributes exactly **zero** to `imagePixels`. The `maxImagePixelsPerSlide = 200000000` cap therefore never fires for sub-pixel frames.
2. **No decoded-image cache, and decode precedes the size check.** `src/render/slide_renderer.cpp:296-300` decodes unconditionally, then checks the frame:

```cpp
const QRectF r = pxRect(e.image.rect, scale, offX, offY);
const QImage decoded = decodeGuarded(e.image.imageData);   // <-- decode happens first
if (decoded.isNull()) { ... } else if (r.width() >= 1 && r.height() >= 1) { ... }
```

Composed: a slide carrying 2,000 `<p:pic>` elements that all reference the **same** large PNG, each with `<a:ext cx="9524" cy="9524"/>`, yields `c.imagePixels == 0`, passes every cap, and then performs 2,000 full decodes of an image bounded only by `setAllocationLimit(128 MiB)`. The loader's media cache shares the *encoded* bytes (correctly), but nothing caches the *decoded* raster.

This is precisely the scenario the validation report says the fourth cap was added to prevent — it records "2000 pictures of one 31 Mpx image ≈ 309 s". That measurement was taken with large frames. Shrink the frames and the stall returns with the cap reading zero. TM-018 is the report's self-declared "highest-severity live-presentation threat" and is marked **Pass**.

Note also that `kMaxImagePixels = 40 Mpx` (`slide_renderer.cpp:24`) is dead code: `setAllocationLimit(128)` binds first for any 4-byte-per-pixel format (128 MiB ÷ 4 ≈ 33.5 Mpx).

**Threat Model.** Actor: anyone who supplies a deck. Impact: the projector freezes mid-talk for minutes on a chosen slide, with no dialog and no cancellation path. This is the exact failure the product exists to prevent.
**Severity:** High. **Exploitability:** Easy — a text edit to one attribute in one XML part; no memory-safety knowledge required.
**Remediation.** Cap on *decoded* pixels, not declared frame area: hoist an LRU cache of decoded `QImage`s keyed by media part name into the render path, count each *distinct* media part once, and enforce a per-slide decode budget (count and cumulative pixels) that is charged at decode time rather than estimated from geometry. Remove the truncating division — compute in EMU² against an EMU² threshold, or use `__int128`/checked arithmetic.

---

#### **SEC-02 — Attacker-reachable signed-integer overflow (undefined behaviour) in the same expression (HIGH)**

Same line, `pre_render_worker.cpp:36`. `rect.cx`/`rect.cy` originate at `deck_loader.cpp:163-164`:

```cpp
rect.cx = attrLocal(ext, "cx").toLongLong();
rect.cy = attrLocal(ext, "cy").toLongLong();
```

They are **unclamped** and attacker-controlled up to `LLONG_MAX` (values beyond it fail `toLongLong` and yield 0, so `9223372036854775807` is the usable maximum). With `cx = cy = LLONG_MAX`: `w / 9525 ≈ 9.68e14`, and the product ≈ `9.4e29`, which overflows `long long` (max `9.22e18`). **Signed overflow is undefined behaviour, not a wrap** — and the `px > 0` guard is evaluated *after* the overflowing multiply, so it cannot prevent it. The overflow threshold is roughly `cx·cy > 2.9e13` EMU each, well within a legal `xsd:long` attribute.

Consequences are twofold: UB on a live input path, and — under the wrapping the compiler will in practice emit — a negative product that silently skips `imagePixels` accumulation entirely, giving a *second* independent evasion of the SEC-01 cap.

This is a thirty-second find for a fuzzer, and it is the direct empirical rebuttal to any reading of TM-021 as "probably fine." The identical bug class was already found twice in this codebase under adversarial review (`parsePercentAttr`, `deck_loader.cpp:505`, and `sourceRectImpl`, `slide_renderer.cpp:210`) and fixed *there* with explicit 64-bit widening and range checks — the lesson was applied locally, not systematically.

**Threat Model.** Actor: anyone who supplies a deck. Impact: UB on the render thread; at minimum a cap bypass, at worst compiler-dependent misbehaviour. Under UBSan this aborts.
**Severity:** High. **Exploitability:** Trivial to trigger; not obviously weaponizable beyond DoS/cap-bypass.
**Remediation.** Range-check `cx`/`cy` at parse time in `parseXfrm` against a sane EMU bound (a slide is ~1.2e7 EMU wide; anything past ~1e11 is meaningless), and use checked or 128-bit arithmetic in `measureComplexity`. Add a UBSan build to CI so this class cannot recur silently.

---

#### **SEC-03 — Decompression amplification bypass, known and deferred, while TM-014 is marked "Pass" (HIGH)**

`deck_loader.cpp:1010-1023`: slide parts are read inside the per-slide loop with **no cache keyed by part name**:

```cpp
for (const QString& rid : slideRids) {
    ...
    const QString slidePart = resolveTarget(QStringLiteral("ppt"), target);
    QByteArray slideXml;
    if (!readPart(za, slidePart, slideXml, limits.maxPartUncompressed)) { ... }
```

`presRels` maps relationship IDs to targets, and **nothing requires those targets to be distinct**. A `<p:sldIdLst>` containing 300 `<p:sldId>` elements whose `r:id`s all resolve to the same 128 MB part causes 300 × 128 MB = **38.4 GB of inflate work, charged as 128 MB** against the 1 GB cumulative cap — because that cap walks the central directory and counts each entry exactly once (`deck_loader.cpp:864-893`). Layouts, masters, theme and media *are* cached (`layoutCache`, `masterBgCache`, `mediaCache`); slide parts and slide rels are not.

It is also **un-cancellable**. `DeckLoader::load(const QString&, const LoaderLimits&)` takes no cancellation token and checks no flag in its body; `DeckLoadWorker` tests `cancelled_` only *before* the parse (`deck_load_worker.cpp:56`) and *after* it returns (line 88). A long load cannot be aborted, and the teardown path then reaches `QThread::terminate()` against a thread suspended inside libzip inflate.

This is not a new discovery — the project's own UAT-3 found it (triage item 12, re-judged SEV-3) and it was **deferred** into `BUGS.md` BUG-29, "UAT-3 SEV-3/SEV-4 set (19 items)", whose row text is itself truncated mid-sentence at "*The 1 GB decompression cap is charged per zip entry but slide/…*". TM-014 ("Decompression bomb through libzip, incl. compression-method confusion") is marked **Pass** in the gate artifact with no reference to it.

**Threat Model.** Actor: anyone who supplies a deck. Impact: minutes-long unkillable hang at deck-open, plus a `QThread::terminate()` on a thread that may hold the allocator lock.
**Severity:** High. **Exploitability:** Easy; requires only a crafted relationship table.
**Remediation.** Route **every** part read through a single cached accessor with a running byte budget charged on cache *miss* — the fix the project's own triage already specified. Add a `const std::atomic<bool>&` cancellation token to `DeckLoader::load`, checked at the top of the per-slide loop.

---

**Positive findings in this category, verified:**
- Magic-byte allow-list (`slide_renderer.cpp:48`) genuinely runs **before** any `QImageReader` is constructed, restricting decode to PNG and JPEG and keeping the CVE-prone TIFF/WebP/GIF/EMF codecs off attacker bytes. `SECURITY.md`'s wording of this claim is accurate. (The *validation report* misattributes the file — see SEC-11.)
- Per-box paragraph/run/character caps, per-slide shape cap, slide-count cap, group-depth cap: all present and enforced.
- `parsePercentAttr` correctly range-checks before a float→int narrowing cast, with the reasoning documented.
- `grammarJson` (`vosk_recognizer.cpp:69`) builds rather than interpolates the grammar, filters to `[a-z ]`, and then **verifies the `[unk]` escape hatch survived the filter by inspecting the emitted JSON** rather than the input phrases. That is exactly the right instinct and it is rare to see.

---

### 3.4 Data Protection

**Finding.** The data handled is: the deck (classified **Confidential** by the project's own Bible §8), and live room audio. Neither is encrypted at rest by the application, and neither needs to be — **the application never persists either**, which I verified rather than assumed (see 3.5). There is no cryptography in the product beyond `QCryptographicHash::Sha256` used for a non-security purpose (`sha256Short`, `deck_load_worker.cpp:39`) — a short content identifier that is computed and, as TM-009 correctly records, **never persisted or used anywhere**. There are no keys to manage.

Data classification exists on paper (Bible §8) and is honoured in the code's containment discipline. Retention is trivially satisfied: nothing is retained.

**Two gaps.**

#### **SEC-09 — Crash artifacts are not suppressed; TM-011's "Pass" covers the wrong path (MEDIUM)**

TM-011 is "Confidential deck and room audio exfiltrated through crash artifacts," and its recorded mitigation is `app_shell.cpp` (`_exit` on the quit path) plus build-time model extraction. But `_exit(0)` governs the **clean quit** path; crash artifacts are by definition produced on the **crash** path, which `_exit` never reaches. I found no `setrlimit(RLIMIT_CORE, 0)`, no `PT_DENY_ATTACH`, and no scrubbing of the decoded deck or the PCM ring buffer anywhere in `src/` (grep for `setrlimit|RLIMIT_CORE|ptrace|mlock|memset_s|explicit_bzero`: zero hits). The process holds the full decoded Confidential deck and live microphone audio in heap. If core dumps are enabled on the host, a crash writes both to disk. Given a parser with four historical memory-safety defects, no fuzzing, and the two fresh defects above, a crash is a realistic event, not a hypothetical.

**Severity:** Medium. **Exploitability:** Requires a crash plus core dumps enabled; an attacker who can supply a deck influences the first half.
**Remediation.** `setrlimit(RLIMIT_CORE, {0,0})` at the top of `main()`. Consider `PT_DENY_ATTACH` in release builds. Neither is expensive; both directly serve a threat already in the model.

#### Informational — memory hygiene and peak usage
`sha256Short` reads the entire file via `f.readAll()` (`deck_load_worker.cpp:109`) *in addition to* libzip's own reads, roughly doubling peak memory on the load path — for a hash that is never used. `parseRun` materialises the full `<a:t>` text before applying `.left(maxChars)` (`deck_loader.cpp:302`), a transient bounded by the 128 MB part cap. Neither is a vulnerability; both are waste on a path where memory pressure is the failure mode.

**Severity:** Informational.

---

### 3.5 Secrets and Credential Hygiene

**Finding.** **Clean, and verified.** No hardcoded secrets, API keys, tokens, passwords, or private key material anywhere in `src/`, `CMakeLists.txt`, or `cmake/` — I swept for `api[_-]?key`, `password`, `secret`, `token`, and PEM headers; every hit was a comment about speech "tokens" in the number parser. `.gitignore` covers `.env`, `.env.local`, `.env.production`, `.env.*.local`, `*.pem`, `*.key`, `*.p12`, `*.keystore`, `credentials.json`. `gitleaks` runs in CI over **full history** (`fetch-depth: 0`, `gitleaks git --redact --exit-code 1`) with the binary's checksum verified against the published `checksums.txt` before extraction — a genuinely well-built supply-chain step. `docs/test-results/2026-08-10_gitleaks_pass.json` is archived.

There are no secrets to expose because the application has no counterparty. This is the strongest category in the review.

**Severity:** Informational (no finding). **Exploitability:** N/A.
**Remediation.** None.

**Note on the "no disk writes" claim, which belongs here:** I verified it and it is **true of `src/`**. Every file open in the product is read-only — `QFile` with `QIODevice::ReadOnly` (`deck_load_worker.cpp:108`), `QBuffer` read-only (`slide_renderer.cpp:67`), `zip_open` with `ZIP_RDONLY` (`deck_loader.cpp:857`). There is no `QSaveFile`, `QTemporaryFile`, `QTemporaryDir`, `QSettings`, `QStandardPaths`, `ofstream`, `fopen`, or `mkstemp`. A prior tester independently confirmed this behaviourally by snapshotting the filesystem around hostile-deck runs. **The claim is true of the application's own code and false of the process** — see SEC-04.

---

### 3.6 Dependency and Supply Chain Security

**Finding.** Dependencies are vendored and pinned, and the integrity story is **verifiably correct**. I re-computed SHA-256 over every vendored artifact and all four match `third_party/PROVENANCE.md` exactly:

| Artifact | Recomputed SHA-256 | Matches PROVENANCE |
|---|---|---|
| `miniaudio.h` | `ac7af4de…c89` | ✅ |
| `vosk_api.h` | `4c96a346…261` | ✅ |
| `libvosk.dyld` | `82fdfba0…fdf` | ✅ |
| `vosk-model-small-en-us-0.15.zip` | (per PROVENANCE, LFS-materialised, 41 MB present) | ✅ |

The provenance narrative is honest about its own irregularities (0.3.45 has no macOS build so 0.3.44 is pinned; no `v0.3.44` git tag exists so the header comes from `v0.3.45` and was ABI-verified symbol-by-symbol with `nm -gU`). That is the standard of disclosure I want and rarely get.

**CVE-2026-32837 (miniaudio 0.11.25) — the unreachability claim is sound.** `src/audio/miniaudio_capture.cpp:6-12` defines `MA_NO_DECODING` (with `MA_NO_ENCODING`, `MA_NO_GENERATION`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_NODE_GRAPH`) **before** `#include "miniaudio.h"`, in the single translation unit carrying `MINIAUDIO_IMPLEMENTATION`. That compiles out the `ma_dr_wav` family entirely, and the application never decodes an audio *file* — it consumes PCM frames from a capture device. I accept this finding as correct.

**But the control protecting it does not exist.** `PROVENANCE.md` states, correctly and prominently: *"If anyone ever removes `MA_NO_DECODING`, this CVE becomes live in one line."* I searched `tests/`, `scripts/`, `.github/`, and `CMakeLists.txt` for any assertion pinning that define: **zero hits**. A one-line regression that reactivates a known CVE is guarded by a comment. Per this review's standing rule — documented intent is not a control — this is **Not Present**.

#### **SEC-05 — No dependency vulnerability scan has ever run (HIGH)**

This is the most consequential supply-chain finding, and it is a closed loop:

1. **CI's OSV step is disabled by construction.** `.github/workflows/ci.yml:83-91` gates `osv-scanner` on `if: ${{ hashFiles('vcpkg.json', 'conan.lock') != '' }}`. Neither file exists in this repository, so the step never runs. The fallback (line 92) is literally `run: echo "No C++ dependency manifest … audited via the Phase 3 SBOM + Snyk pass."` — a green step that scans nothing and **defers to Phase 3**.
2. **Phase 3's Snyk scan is a SKIP.** `docs/test-results/phase3/summary-2026-08-10T20-53-27Z.md`: `| snyk | real | SKIP | yes | - | snyk not authenticated — run 'snyk auth' …`. Attested, therefore non-blocking.
3. **The SBOM cannot close the gap either.** `sbom.json` describes all seven components with `pkg:generic/…` purls and contains **zero `cpe` entries** (verified: `grep -c cpe sbom.json` → 0). Generic purls match nothing in OSV, Snyk, or Grype — vulnerability matching requires an ecosystem purl or a CPE. Even if the Snyk scan were authenticated and pointed at this SBOM, it would return zero findings for structural reasons.

Net effect: **Qt 6.11.1, libzip 1.11.4, pugixml 1.16, Vosk 0.3.44, and the Vosk model have never been checked against any vulnerability database, by any tool, at any time.** The single known CVE in the tree was found by a human reading release notes. Qt and libzip in particular are large, actively-CVE'd C/C++ codebases sitting directly on the untrusted-input path.

`docs/test-results/2026-08-10_dependency-vulnerability-review.md` exists as a manual analysis, and manual review is better than nothing — but a point-in-time human read is not an update mechanism, and there is no process, cadence, or owner defined for responding to a future dependency CVE.

**Threat Model.** Actor: any deck supplier, exploiting a known-and-published libzip or Qt image-codec vulnerability. The project would have no way to learn it was exposed.
**Severity:** High. **Exploitability:** Depends entirely on the unknown CVE set — which is the point.
**Remediation.** (a) Add CPEs to every SBOM component (`cpe:2.3:a:qt:qt:6.11.1:*:*:*:*:*:*:*` and equivalents) so the SBOM becomes machine-matchable; (b) run Grype or `osv-scanner --sbom sbom.json` in CI as a **blocking** step — it consumes CycloneDX directly and needs no package manager, removing the "C++ has no canonical manifest" objection entirely; (c) delete the vacuous echo step, which currently launders an absence of scanning into a green check; (d) add a CI assertion that `MA_NO_DECODING` is defined.

---

### 3.7 Error Handling and Information Leakage

**Finding.** The design here is deliberate and, for the *application's own* channels, effective. `describeLoadError` (`deck_load_worker.cpp:16-37`) is a genuinely closed vocabulary — a `switch` over `LoadErrorKind` returning eight fixed strings, none containing a path, filename, byte, or offset. `LoadError::message` does carry the path, and I traced it: it is set in `deck_loader.cpp` and reaches no widget. The exception boundaries in `DeckLoadWorker::start` and `PreRenderWorker::renderOne` both discard exception text explicitly, with the reasoning recorded in-line ("*an exception message can carry deck content*"). `Notice` builds every string from an enum id plus integers. There is **no logging framework at all** — `qDebug`, `qInfo`, `qWarning`, `qCritical`, `qFatal`, `printf`, `std::cout`, `std::cerr`, `NSLog`: zero hits in `src/`. Vosk's stderr logger is silenced via `vosk_set_log_level(-1)` in the `VoskEngine` constructor, which does execute before `vosk_model_new` (same object, `vosk_engine.cpp:56` vs `:84`). There are no debug or verbose modes.

That is a high standard, met — for the code the project wrote. It is not met for the code it links.

#### **SEC-04 — Live, reproduced, unremediated content-disclosure channel, marked "Pass" (HIGH)**

`tests/uat/sessions/2026-08-05-session-3/submissions/uat-3-triage.md`, item 13 (SEV-3), reproduced by a prior tester against the real binary:

> `-> stderr: libpng error: ACQU: CRC error` — "The four bytes 'ACQU' are read verbatim out of the media part inside the deck."

The tester's analysis is correct and I confirm the mechanism: Qt's own decoder messages are tagged `qt.gui.imageio:` and *are* suppressible via `QLoggingCategory`, but the bare `libpng error:` line comes from **libpng's default error handler writing to `stderr` directly**, filtered by nothing. For a `.app` launched from Finder — which is how this is actually started — stderr is captured by the macOS unified logging system and persisted to disk **outside the application's control**. The tester also noted the part that matters most: this applies to the *Confidential* deck, not only to hostile ones — any image that trips a libpng warning path emits bytes from that image into the system log.

**Current status, verified today:**
- No `qInstallMessageHandler` anywhere in `src/`.
- No stderr redirection, `freopen`, `dup2`, or `setvbuf` anywhere in `src/`.
- No PNG chunk-structure pre-validation in `decodeGuarded` — the magic-byte gate checks the first 3–8 bytes only, then hands the buffer to libpng.
- **No entry in `BUGS.md`.** `grep -i libpng BUGS.md` → zero hits. `grep -i libpng CHANGELOG.md` → zero hits. The only stderr work ever done was silencing Vosk.
- It survives only as one clause inside BUG-29, "UAT-3 SEV-3/SEV-4 set (19 items)", deferred by pointer to a triage file, with a rationale that addresses stage impact rather than the disclosure.

**And yet TM-011 and TM-012 are both marked "Pass"** in `2026-08-10_threat-model-validation.md`. The validation method recorded for TM-011 is: *"Inspection each audit: no file opened for writing in `src/`."* That method is **structurally incapable of detecting this channel**, because the writer is libpng and the file is owned by the OS. The report's Pass therefore rests on a test that cannot fail for the reason the threat exists.

To be fair to the project: the tester's own severity call (SEV-3, low volume, 4 bytes per malformed chunk, never reaches the projector) is reasonable, and this is not bulk exfiltration. My objection is not primarily to the severity — it is that **a known-open content-leak was carried into a gate artifact as "Pass"**, and that the accepted-risk table the owner signed on 2026-08-10 does not list it, so the owner accepted eight risks without this being one of them.

**Threat Model.** Actor: passive — anyone with subsequent read access to the host's unified log (support, MDM log collection, backup, forensic acquisition, or a later local attacker). Impact: fragments of Confidential deck media content persisted outside the process.
**Severity:** High as a **governance** failure (false "Pass" on a signed gate artifact); Medium as a **technical** exposure (low volume, no attacker control over timing, requires log access).
**Remediation.** (a) Correct the TM-011/TM-012 rows to "Partial" and add this to the accepted-risk table so the owner can accept it explicitly, or fix it; (b) technically, validate PNG chunk structure and JPEG marker structure in `decodeGuarded` before the buffer reaches libpng — `decodeGuarded` already parses enough to call `format()` and `size()`, so this is a small extension; (c) install a `qInstallMessageHandler` in `main.cpp` that drops the `qt.gui.imageio` category; (d) replace the inspection method for TM-011 with a behavioural test that runs the binary against a malformed-PNG fixture and asserts stderr is empty.

---

### 3.8 Logging and Audit Trail

**Finding.** **There is no logging of any kind.** No security events, no authentication events (there is no authentication), no file-access records, no configuration-change records — no log file, no log framework, no telemetry. This is a deliberate product decision, not an oversight: TM-011 forbids the log that TM-008 ("unattributable slide changes after an incident") and TM-009 ("no record of which deck was rendered") would require. The validation report states the conflict plainly and records that confidentiality was chosen. `sha256Short` exists as the deck identifier such a log would have used, and is computed but never persisted — which is consistent with the decision.

I want to be explicit: **for this product, this is the right call, and the reasoning is sound.** A presentation tool that logs which deck was opened, when, and what was said in the room would be a worse security artifact than one that logs nothing. The trade was identified, documented, escalated, and accepted by a named owner on a date. That is how a conflict between two security requirements is supposed to be resolved.

**But the consequences are categorical and must be stated for an organizational deployment:** there is no forensic capability whatsoever. If this application is ever implicated in an incident — a deck disclosed, a machine compromised, a question about what was opened — there is no evidence to collect from it. Logs cannot be tampered with because they do not exist. No compliance framework that requires audit logging can be satisfied by any configuration of this application (see 3.9).

**Threat Model.** Actor: post-incident investigator, or an auditor. Impact: zero attribution, zero reconstruction.
**Severity:** Medium — accepted, correctly reasoned, but non-negotiable for regulated deployment. **Exploitability:** N/A.
**Remediation.** None for the accepted scope. For any organizational deployment, the honest path is not to add logging (which breaks TM-011) but to **scope the application out of environments that require audit trails** — and to state that in `SECURITY.md` as a deployment restriction rather than leaving it as an accepted risk.

---

### 3.9 Compliance Framework Compatibility

**Finding.** The application processes no cardholder data, no PHI, and no financial records — it renders whatever `.pptx` it is given. Compliance posture therefore depends entirely on what a *user* puts in a deck, and the application provides no control that would make it safe to put regulated data there.

The two disqualifying properties for every framework below are (1) **no audit logging by design**, and (2) **no code signing, notarization, or integrity verification of the installed application**.

| Framework | Key requirement | Status | Gap |
|---|---|---|---|
| **PCI-DSS v4.0** | Req. 10 (log all access to CHD, retain 12 months); Req. 6.3 (patch known vulns); Req. 12.10 (incident response) | **Not compatible** | No logging *by design* — Req. 10 is unsatisfiable without breaking TM-011. No dependency CVE process (SEC-05) fails 6.3.3. No `docs/INCIDENT_RESPONSE.md` (Phase 4 artifact, not yet written). Must not be used to display CHD. |
| **HIPAA Security Rule** | §164.312(b) audit controls; §164.312(c)(1) integrity; §164.308(a)(1)(ii)(D) information system activity review | **Not compatible** | §164.312(b) requires "hardware, software, and/or procedural mechanisms that record and examine activity" — there are none. §164.312(c)(1) integrity is unmet: ad-hoc signing, no notarization, no runtime integrity check. Must not be used to display PHI. |
| **SOC 2 Type II** | CC6.1 logical access; CC7.2 anomaly detection; CC7.3 incident evaluation; CC8.1 change management | **Partially compatible** | CC7.2/CC7.3 produce **no evidence** — nothing is monitored or recorded. CC8.1 is unusually *strong*: `APPROVAL_LOG.md`, CI-enforced phase gates, `.claude/phase-state.json`, and a documented Build Loop are real change-management evidence. CC6.1 is N/A (no logical access boundary). A SOC 2 report could cover the *development process*; it could not cover the *operation* of this app. |
| **SOX (ITGC)** | Access controls, change management, segregation of duties | **Partially compatible** | Change management is genuinely evidenced. **Segregation of duties fails**: this is a single-maintainer project where the author, the approver, and the risk acceptor are the same person. `CLAUDE.md` acknowledges this directly — branch protection with independent reviewers is "recommended," not enabled, and "the Orchestrator creates and merges their own PRs." No independent approval exists anywhere in the chain, including for the eight accepted risks. |
| **FedRAMP (Moderate)** | AU-2/AU-3 audit events; CM-14 signed components; RA-5 vulnerability scanning; SI-2 flaw remediation | **Not acceptable** | AU family: unsatisfiable by design. CM-14 (signed components): ad-hoc signature only. RA-5: **no vulnerability scanning has ever run** (SEC-05) — this alone is disqualifying. SI-2: no flaw-remediation process for dependencies. Not a candidate. |

**Severity:** High for any regulated deployment. **Exploitability:** N/A — this is a fitness determination.
**Remediation.** Do not pursue compliance retrofits. The correct response is a **documented scope exclusion** in `SECURITY.md`: this application must not be used to display regulated data, and the reason (no audit trail, by deliberate design) should be stated as a feature of the threat model rather than a defect to be fixed.

---

### 3.10 Local Privilege and Sandbox

**Finding.** OS permissions requested are **minimal and each is justified**: microphone (`NSMicrophoneUsageDescription`, `cmake/MacOSXBundleInfo.plist.in`) and user-initiated file read. No camera, no contacts, no location, no full-disk access, no accessibility API, no admin elevation, no helper tool, no launch agent. The microphone consent string is honest and specific — *"Speech is recognised entirely on this Mac, is never recorded to disk, and is never sent anywhere"* — and, unusually, that string is **true**, which I verified in 3.5 and 3.7. Most consent strings are marketing; this one is a security claim the code actually honours.

This is not Electron or Tauri. There is no web renderer, no JS bridge, no `nodeIntegration` question. The UI layer is Qt widgets in-process; there is no UI→native boundary to escalate across because there is no boundary.

**But the process itself is unhardened, and this undermines the review's headline claims.**

#### **SEC-04b / SEC-07 — No runtime hardening; the "structural" properties are source-level, not runtime (HIGH)**

Verified absences:

| Control | Status | Evidence |
|---|---|---|
| Hardened runtime | **Not present** | `scripts/make-test-build.sh:62-63` — `codesign -f -s - --deep`. No `--options runtime`. |
| Code signature identity | **Ad-hoc** (`-s -`) | Not Developer-ID; not notarized. Correctly disclosed in `SECURITY.md` and as TM-003. |
| Entitlements | **Not present** | No `*.entitlements` file exists anywhere in the tree. |
| Library validation | **Not present** | Requires hardened runtime. |
| `-fstack-protector-strong` | **Not present** | No `target_compile_options`/`add_compile_options` in `CMakeLists.txt` at all. |
| `-D_FORTIFY_SOURCE=2` | **Not present** | As above. |
| `-D_GLIBCXX_ASSERTIONS` | **Not present** | As above. |
| Sanitizers in CI | **Not present** | `.github/workflows/ci.yml` — jobs are `test` and `sast` only; zero matches for `sanitize|asan|ubsan|tsan`. |

Consequences, in order of importance:

1. **`DYLD_INSERT_LIBRARIES` works.** Hardened runtime is the control that blocks dyld injection. Without it, **any local process running as the user can inject arbitrary code into this application at launch** — a process that holds the decoded Confidential deck in heap and has a live microphone stream. Injected code can trivially add the network egress that "there is no network code" is meant to preclude.
2. **`SECURITY.md`'s framing is therefore overstated.** It presents three properties — no network, no disk writes, speech never leaves the process — as *"enforced structurally rather than by convention."* What actually enforces them is **the absence of the corresponding code in `src/`**. That is a strong property against *accidental* regression (and it is genuinely well maintained), but it is not a runtime control and it does not survive an attacker with local code execution or the ability to modify the bundle. The table's "How it is enforced" column should say "by construction, within the application's own source" — which is true and still creditable — rather than implying a mechanism that constrains the running process.
3. **A memory-safety exploit in the parser (T1) faces no mitigations** beyond what the platform provides by default: no stack cookies from the project's own flags, no fortified libc calls, no hardened runtime to constrain post-exploitation.

The project's own model captures the dylib angle honestly as TM-022/TM-023 and marks both **Deferred to Phase 4**, which is the correct disposition — my objection is to the `SECURITY.md` framing, not to the threat model's treatment.

#### **SEC-08 — Microphone TCC grant is inheritable (MEDIUM)**

Not present in the threat model in any form. macOS binds TCC grants (here, microphone consent) to an application's code-signing identity. With an **ad-hoc** signature and no hardened runtime, that binding is weak: an attacker who can write to the installed bundle can modify the application and **inherit the user's existing microphone consent without triggering a new prompt**. TM-022/TM-023 cover code execution via dylib and plugin-path hijacking; neither covers permission inheritance, which is a distinct and arguably worse outcome — a silently repurposed microphone in a room where confidential discussions happen.

**Threat Model.** Actor: local malware or a second user on a shared machine. Impact: covert room audio capture under the presentation app's identity.
**Severity:** Medium. **Exploitability:** Requires local write access to the bundle — which, on a single-user Mac with the app in `~/Applications` or `~/Downloads`, is exactly what any user-level process already has.
**Remediation.** Developer-ID signature + `--options runtime` + notarization + `com.apple.security.cs.disable-library-validation` **omitted** (i.e. leave library validation on). Add TCC inheritance to the threat model as TM-024.

---

### 3.11 IPC and Process Security

**Finding.** **No IPC exists**, and I verified this rather than assuming it: no `QLocalSocket`, no `QLocalServer`, no named pipes, no Mach ports opened by the application, no D-Bus, no shared memory, no XPC service, no helper tool, no child processes of any kind (`QProcess`, `system`, `exec*`, `popen`, `fork`, `posix_spawn`, `NSTask`: zero hits across `src/`). The application is a single process. No local process can communicate with it because there is nothing listening.

Internal concurrency is thread-based (GUI thread, `DeckLoadWorker` thread, `PreRenderWorker` thread, and miniaudio's real-time capture thread), which is an in-process trust domain, not an IPC boundary. The threading work is, for what it is worth, notably careful — exception boundaries on every worker slot with recorded reasoning, atomic cancellation flags, a re-entrancy guard in `RecognizerController`, and a documented `_exit(0)` on the quit path to avoid racing static destructors against a live render thread.

One residual worth flagging, already known to the project: `BUGS.md` item 14 (UAT-3) documents that `QPointer`'s check-then-use is not atomic across threads, reproduced at ~6% under key pressure, described by the finder as "an unsynchronised cross-thread dereference of an object mid-destruction — undefined behaviour on the live path." Status: within the deferred BUG-29 bundle. Observable impact is benign (a lost re-steer), but it is UB in the same process that parses untrusted input, and TSan is clean on it because it is a lifetime/ordering bug rather than a data race — meaning the project's sanitizer evidence structurally cannot cover it.

**Severity:** Low (no IPC surface); Medium for the known cross-thread lifetime UB. **Exploitability:** Not attacker-controlled.
**Remediation.** None for IPC. For the lifetime bug, apply the fix the finder specified: replace the cross-thread `invokeMethod`-by-name with a signal, which Qt emits under its own connection lock and which handles receiver destruction correctly.

---

### 3.12 Update Channel Security

**Finding.** **There is no update channel.** No Sparkle, no auto-updater, no version check, no download logic, no network code of any kind (3.1). Distribution is a manually-built `.app`. There is consequently nothing to hijack via DNS spoofing or MITM — the entire category is closed by the absence of the mechanism, which is the strongest form of the control.

The corollary is the real risk: **there is no mechanism to deliver a security fix.** If a critical vulnerability is found in this application or in Qt, libzip, or pugixml, there is no path by which an installed instance learns of it or is updated. Combined with SEC-05 (no vulnerability scanning at all), the project would neither detect nor be able to remediate a dependency CVE in a deployed copy.

`.github/workflows/release.yml` is documented in the project's own UAT (item 15) as an invalid workflow file — `- uses: # TODO: Add setup action for your language` is a schema error — so no signed or checksummed build artifact can currently be produced at all. The practical consequence today is that the only build in existence is the one in the author's build tree, unsigned beyond ad-hoc and unnotarized.

Rollback: no mechanism, and none needed at current distribution scale (n=1).

**Threat Model.** Actor: none, for hijacking. The threat is operational: an unpatchable, undetectable, unrecallable binary.
**Severity:** Low today (single known instance); **High** the moment distribution begins — which is exactly what Phase 4 is.
**Remediation.** Before any distribution: fix `release.yml`; produce Developer-ID-signed, notarized, checksummed artifacts; publish checksums alongside; and define a vulnerability-response process with a named owner and a stated cadence. If an auto-updater is ever added, it becomes the highest-value target in the product and must be signed-payload-verified — but the correct Phase 4 answer is almost certainly *no updater*, with a documented manual-replacement process instead.

---

## 4. Security Controls Matrix

Classification per the review's standing rule: **only mechanical enforcement counts.** Documented intent, comments, and process discipline are classified Advisory regardless of how well-reasoned they are.

| # | Control | Location | Classification |
|---|---|---|---|
| 1 | No network code in the application | `src/` (verified absent) | **Enforced** (by construction, source-level) |
| 2 | No write-path to disk in the application | `src/` (all opens read-only) | **Enforced** (source-level) |
| 3 | No process execution / no shell | `src/` (verified absent) | **Enforced** (source-level) |
| 4 | No IPC surface | `src/` (verified absent) | **Enforced** |
| 5 | No logging framework; speech never stored/logged/rendered | `vosk_engine.cpp`, `recognizer_controller.cpp` | **Enforced** |
| 6 | Vosk decoder stderr silenced before model load | `vosk_engine.cpp:60` | **Enforced** |
| 7 | Third-party (libpng) stderr silenced | — | **Not Present** (SEC-04) |
| 8 | ZIP caps enforced from central directory pre-read, unsigned compare | `deck_loader.cpp:864-893` | **Enforced** |
| 9 | Cumulative decompression cap is amplification-proof | `deck_loader.cpp:1019` (no slide-part cache) | **Partially Enforced** (SEC-03) |
| 10 | Load operation is cancellable | `DeckLoader::load` (no token) | **Not Present** (SEC-03) |
| 11 | Iterative XML descent (stack-exhaustion defence) | `deck_loader.cpp:43` | **Enforced** |
| 12 | Group-nesting depth cap (32) | `deck_loader.cpp:610` | **Enforced** |
| 13 | XXE / entity expansion contained | pugixml defaults + `SEC/TM-010` tests | **Enforced** |
| 14 | Zip-slip contained (nothing extracted) | `deck_loader.cpp` `readPart`/`resolveTarget` | **Enforced** |
| 15 | Per-slide shape / paragraph / run / character caps | `deck_loader.hpp:40-46` | **Enforced** |
| 16 | Image magic-byte allow-list before decoder construction | `slide_renderer.cpp:48` | **Enforced** |
| 17 | Decoder allocation limit (128 MiB) | `slide_renderer.cpp:69` | **Enforced** |
| 18 | Declared-image-pixel render cap (TM-018) | `pre_render_worker.cpp:36` | **Not Present** — measures frame geometry, evadable two ways (SEC-01, SEC-02) |
| 19 | Integer-overflow safety on deck-supplied geometry | `pre_render_worker.cpp:36` | **Not Present** (SEC-02) |
| 20 | Integer-overflow safety on percentage/crop attributes | `deck_loader.cpp:505`, `slide_renderer.cpp:210` | **Enforced** |
| 21 | Raster cache bounded at 2 GB | `raster_cache.hpp:21` | **Enforced** |
| 22 | Closed error vocabulary (no path/bytes to UI) | `deck_load_worker.cpp:16-37` | **Enforced** |
| 23 | Exception boundaries on worker threads | `deck_load_worker.cpp:73`, `pre_render_worker.cpp:104` | **Enforced** |
| 24 | Grammar-capable model required (dynamic graph) | `vosk_recognizer.cpp:110` | **Enforced** |
| 25 | Every grammar word verified against model vocabulary | `vosk_engine.cpp:91` | **Enforced** |
| 26 | `[unk]` escape hatch verified in emitted JSON | `vosk_recognizer.cpp:158` | **Enforced** |
| 27 | Voice gated while Paused; keyboard independent | `recognizer_controller.cpp`, `key_translator.cpp` | **Enforced** |
| 28 | Audience cannot issue commands | — | **Not Present** (accepted: TM-001/002) |
| 29 | Dependency pinning + SHA-256 provenance | `third_party/PROVENANCE.md` (re-verified ✅) | **Enforced** |
| 30 | Build-time model hash verification | `cmake/VendorVosk.cmake` | **Enforced** |
| 31 | `MA_NO_DECODING` (CVE-2026-32837 mitigation) | `miniaudio_capture.cpp:8` | **Advisory** — correct, but no test or CI check pins it |
| 32 | Dependency vulnerability scanning | CI step disabled; Snyk SKIP | **Not Present** (SEC-05) |
| 33 | Machine-actionable SBOM (CPE/ecosystem purls) | `sbom.json` (generic purls, 0 CPEs) | **Not Present** (SEC-05) |
| 34 | SAST covering C++ | Semgrep `p/owasp-top-ten` | **Not Present** — proven vacuous for C++ by the project's own positive control |
| 35 | Secret scanning over full history | `ci.yml:71-81` (gitleaks, checksum-verified) | **Enforced** |
| 36 | Sanitizers (ASan/UBSan/TSan) in CI | — | **Not Present** — claimed by TM-021, absent from `ci.yml` |
| 37 | Fuzzing of the untrusted-input parser | — | **Not Present** (accepted: TM-021) |
| 38 | Compiler hardening flags | `CMakeLists.txt` | **Not Present** |
| 39 | Hardened runtime / library validation | `make-test-build.sh:63` | **Not Present** (deferred: TM-022/023) |
| 40 | Developer-ID signature / notarization | ad-hoc `-s -` | **Not Present** (accepted: TM-003) |
| 41 | Self-contained bundle (no `/opt/homebrew` refs) | `make-test-build.sh:74-80` (fails build) | **Enforced** |
| 42 | Core-dump / crash-artifact suppression | — | **Not Present** (SEC-09) |
| 43 | Microphone consent string accuracy | `MacOSXBundleInfo.plist.in` | **Enforced** (and true) |
| 44 | TCC grant binding strength | ad-hoc signature | **Not Present** (SEC-08) |
| 45 | Audit trail / forensic record | — | **Not Present** (accepted: TM-008/009) |
| 46 | Update channel integrity | — | **Not Present** (no channel exists) |
| 47 | Change-management evidence (phase gates, approval log) | `APPROVAL_LOG.md`, `ci.yml` governance jobs | **Enforced** |
| 48 | Independent review / segregation of duties | — | **Not Present** — single maintainer; author = approver = risk acceptor |
| 49 | Accepted-risk scope ("own machine, that talk") | prose only | **Advisory** — nothing enforces it (see §7) |

**Tally:** Enforced 27 · Partially Enforced 1 · Advisory 2 · Not Present 19.

---

## 5. Compliance Gap Analysis

| Framework | Readiness | Blocking gaps | Could this be closed? |
|---|---|---|---|
| **PCI-DSS v4.0** | ❌ **Not ready** | Req. 10 (logging) unsatisfiable by design; Req. 6.3.3 (patch known vulns) — no dependency scanning; Req. 12.10 — no incident-response plan | Only by breaking TM-011. Recommend scope exclusion instead. |
| **HIPAA Security Rule** | ❌ **Not ready** | §164.312(b) audit controls — none exist; §164.312(c)(1) integrity — ad-hoc signing, no notarization, no integrity verification | Signing is closable in Phase 4; audit controls are not, by design. |
| **SOC 2 Type II** | ⚠️ **Partial** | CC7.2/CC7.3 produce zero evidence (no monitoring, no logs). **CC8.1 change management is strong** — phase gates, approval log, CI enforcement, documented Build Loop | A SOC 2 report could cover the development process; it cannot cover operation of the app. |
| **SOX (ITGC)** | ⚠️ **Partial** | Change management genuinely evidenced. **Segregation of duties fails** — single maintainer is author, approver, and risk acceptor; branch protection "recommended" but not enabled | Closable only by adding an independent reviewer with merge authority. |
| **FedRAMP Moderate** | ❌ **Not a candidate** | AU-2/AU-3 (audit events) unsatisfiable; CM-14 (signed components) ad-hoc only; **RA-5 (vulnerability scanning) has never run** — independently disqualifying; SI-2 no flaw-remediation process | No. Do not pursue. |

**Bottom line for compliance:** this application should carry an explicit, documented prohibition on displaying regulated data (CHD, PHI, or material non-public financial information). That prohibition is cheap, honest, and consistent with the threat model — far better than a compliance retrofit that would require dismantling TM-011.

---

## 6. Desktop Threat Model

| Vector | Applicable? | Assessment |
|---|---|---|
| **Malicious file (primary)** | ✅ **Yes — the dominant vector** | Arbitrary `.pptx` via dialog, drag-drop, or `argv[1]`. Reaches libzip → pugixml → hand-written C++ parser → Qt image codecs. Defended by good caps (verified) but with three live evasions (SEC-01/02/03), zero fuzzing, no sanitizers in CI, and no compiler hardening. **This is T1 and T2.** |
| **Local attacker — code injection** | ✅ **Yes** | No hardened runtime ⇒ `DYLD_INSERT_LIBRARIES` succeeds against a process holding the Confidential deck and live microphone. Defeats all three headline properties at once. **T4.** TM-022 covers this and is deferred. |
| **Local attacker — bundle tampering** | ✅ **Yes** | Ad-hoc signature, no library validation. Dylibs inside the bundle can be swapped; Qt plugin paths can be steered (TM-023). `make-test-build.sh` verifies self-containment at *build* time; nothing verifies integrity at *run* time. |
| **Local attacker — permission inheritance** | ✅ **Yes — not modelled** | Weak TCC binding ⇒ microphone consent inheritable by a modified bundle without re-prompting. **SEC-08.** Absent from the threat model entirely. |
| **Local attacker — data at rest** | ⚠️ **Partial** | Nothing is persisted by the app (verified). But deck fragments reach the macOS unified log via libpng (SEC-04), and a core dump would contain the full deck and room audio (SEC-09). |
| **Audience / acoustic attacker** | ✅ **Yes** | Every person in the room is an unauthenticated operator. Honestly measured (60/102 fragments, 0/6 sentences), mitigated by Pause + independent keyboard, accepted. Denial of *voice* is achievable; denial of the *deck* is not. |
| **Malicious speech model** | ⚠️ **Partial** | Model hash-verified at build; dynamic-graph presence checked at runtime; every grammar word round-tripped. Residual is an attacker who can already write to the installed bundle = TM-022. Also: the `PPTV_VOSK_MODEL_DIR` build-tree fallback (`vosk_recognizer.cpp:133`) is a third resolution path that should not exist in a release binary. |
| **Update hijacking** | ❌ **No** | No update channel exists. Closed by absence. Corollary: no patch delivery path either. |
| **IPC abuse** | ❌ **No** | No IPC surface exists. Verified. |
| **Network attacker (MITM, SSRF, C2)** | ❌ **No** | No network code. Verified. Caveat: a `file://` path on a network mount is fetched by the kernel; and post-injection (T4) this property is void. |
| **Privilege escalation** | ❌ **No** | No elevation, no helper tool, no setuid, no launch agent. Least privilege genuinely observed. |
| **Supply chain (build-time)** | ⚠️ **Partial** | Hashes verified and correct (I re-checked all four). But **no component has ever been CVE-scanned** (SEC-05), and the SBOM is not machine-matchable. |

---

## 7. Assessment of the Eight Accepted Risks

The owner accepted eight residual risks on 2026-08-10, with the stated scope: *"for a talk on his own machine with his own deck… This acceptance is scoped to that setting and does not carry into any wider distribution."*

**Was the acceptance reasonable?** For the stated scope, **yes — and the process was better than most enterprises manage.** The risks were enumerated, individually rated, presented as three options (accept all eight / accept seven and hold TM-021 / walk through individually), attributed to a named human on a date, and the AI arm explicitly recorded that it did not self-attest. The reasoning — that the only untrusted input in that setting is a file the presenter authored himself — is sound, and it is the correct reasoning. The author's own closing note on TM-021 (*"That is a fair reason to accept it and a bad reason to call it closed"*) is exactly right and I would not improve on it.

**Is the scope enforced by anything?** **No. Nothing enforces it — not one mechanism.**

I looked specifically for: a build flag limiting the binary, an expiry or kill date, a runtime check on deck provenance, an allow-list of decks or hashes, a distribution control, a license gate, a first-run warning, or a documented re-review trigger. **None exists.** The application will open any `.pptx` from any source — email attachment, USB stick, network share — via `Cmd+O`, drag-and-drop, or `argv[1]`, on any machine it is copied to, forever, with no signal that it has left the scope its risk acceptance was granted under.

This creates a specific, predictable failure mode: the acceptance narrows the assumed *input*, but nothing narrows the actual *input*. The moment the binary is copied or a stranger's deck is opened, all eight residuals apply at full strength — most importantly TM-021, rated **High**, about which the report says plainly that it *"should be reopened before this application ever opens a deck someone else supplied."* That reopening is triggered by nothing. It depends entirely on someone remembering.

**And Phase 4 is the trigger.** Phase 4 is distribution — signing, notarization, release, handoff. Entering it is precisely the act that invalidates the "his own machine, his own deck" premise on which all eight acceptances rest. The accepted-risk table should therefore be treated as **expired on entry to Phase 4**, not carried forward. Nothing in the current gate logic does that.

**Recommendation.** Add an explicit expiry to the acceptance table (`Expires: on entry to Phase 4, or on first use with a third-party deck, whichever is first`) and make the Phase 3→4 gate re-present the eight risks for fresh acceptance under the new scope. This is a documentation change, not engineering work, and it converts a latent trap into a checkpoint.

---

## 8. Assessment of the Gate Artifacts Themselves

Because this review is a gate condition, the reliability of the *other* gate artifacts is in scope.

### 8.1 `docs/test-results/2026-08-10_threat-model-validation.md`

The report sets an excellent standard for itself — *"a mitigation is not validated because it exists in the code — it is validated when something demonstrates it works"* — and then does not consistently meet it.

| Claim in the report | Verified? |
|---|---|
| Five new attack payloads committed as `SEC/` tests | ✅ **True.** `SEC/TM-004`, `SEC/TM-007`, `SEC/TM-010`, `SEC/TM-010/017`, `SEC/TM-016` all present in `tests/test_deck_loader.cpp`. |
| Each asserts the deck loads first | ✅ **True.** Verified by reading the tests. |
| *"Every STRUCTURAL row therefore has a test that pins the absence"* | ❌ **False.** TM-005 and TM-012 are validated by "**Inspection**" and no test pins either. TM-011 is likewise "Inspection" and marked Pass. |
| TM-015 mitigation in `deck_loader.cpp` | ❌ **False.** The magic-byte allow-list is in `slide_renderer.cpp:48`. It runs at *render* time on a worker thread, not at *load* time — a material difference for anyone reasoning about where attacker bytes flow. |
| TM-021 mitigation: *"ASan/UBSan/TSan in CI and locally"* | ❌ **False as to CI.** `.github/workflows/ci.yml` contains no sanitizer job. `build-asan/`/`build-tsan/` are local artifacts. The only *continuously enforced* half of the mitigation does not exist. |
| TM-014 "Pass" | ❌ **Contradicted** by the project's own deferred UAT finding (SEC-03), still live in code. |
| TM-018 "Pass" | ❌ **Falsified** — cap evadable two independent ways (SEC-01, SEC-02). |
| TM-011 / TM-012 "Pass" | ❌ **Contradicted** by the unremediated libpng channel (SEC-04); validation method cannot detect it. |
| Summary: *"Pass (structural) — 4 of the 11"* | ❌ **Inaccurate.** Only three rows carry that label (TM-005, TM-012, TM-016). |
| TM-007 duplicate-part determinism | ⚠️ **Weaker than stated.** The test verifies determinism *across runs*; it does not verify that the *benign* entry wins. Resolution order is a libzip implementation detail, not a project-enforced property — so an attacker who can add a duplicate part name controls which one resolves, deterministically. |

Four of the five payload tests are genuinely good work and I credit them. But a gate artifact with three false statements of fact and three "Pass" ratings I could falsify from the code cannot be relied on as evidence in its current form.

### 8.2 `SECURITY.md`

Mostly accurate, and its withdrawal of the earlier false decoder claim is exemplary. Two corrections needed: it cites `localDeckDropPathFrom`, but the function is **`localDeckPathFrom`** (`start_view.cpp:17`); and the "How it is enforced" column overstates *source-level absence* as runtime enforcement (§3.10).

### 8.3 `docs/test-results/phase3/summary-*.md` — the gate cannot fail

| Scanner | Status | What it actually establishes |
|---|---|---|
| `semgrep-full-tree` | PASS, "0 findings" | **Near-nothing.** The project's own positive control (UAT-3 item 16) fed Semgrep a C++ file containing a `strcpy` overflow, `sprintf`+`system()` injection, `execl` with user input, a double free, and a format-string bug: **0 findings**, "Rules run: 5". The scan covered 304 files, most of them `.json`, `.sh`, and `.clang-format`. |
| `license` | SKIP (attested) | Nothing. |
| `snyk` | SKIP (attested — "not authenticated") | Nothing. **This is the step CI defers dependency scanning to.** |
| `zap-dast` | SKIP (attested) | Correctly N/A for desktop. |
| `threat-model` | PASS | *"unmitigated table empty-or-risk-accepted"* — **the gate passes on risk acceptance, not mitigation.** A High-rated unmitigated threat satisfies it as long as someone signs. |

**Overall: PASS** — with two of five scanners contributing no information, one contributing almost none, and the fifth satisfiable by signature alone. The archived run also records **`dirty: yes`**, so the evidence does not correspond to a reproducible commit.

I want to be precise about the criticism: the framework is *honest* — it labels the SKIPs, requires attestation, and prints the reasoning. Nothing is hidden. But a gate that cannot fail is not a gate, and "Overall: PASS" is a stronger signal than the underlying evidence supports.

---

## 9. Hard Stops — conditions under which this project MUST NOT be used

1. **MUST NOT** be used to display cardholder data, PHI, or material non-public financial information. No audit trail exists by design and none can be added without breaking TM-011.
2. **MUST NOT** be deployed in any environment requiring audit logging, forensic reconstruction, or attribution of user actions (PCI-DSS Req. 10, HIPAA §164.312(b), FedRAMP AU family).
3. **MUST NOT** be used to open a `.pptx` from an untrusted or unverified source until a fuzzing campaign has been run against `DeckLoader::load` and SEC-01/02/03 are fixed. This is the project's own stated condition on TM-021 and I fully endorse it.
4. **MUST NOT** be distributed to any third party, internally or externally, without Developer-ID signing, notarization, and hardened runtime. Ad-hoc-signed binaries with library validation disabled must not be installed on machines the distributor does not own.
5. **MUST NOT** be run on a multi-user or shared machine, or on any machine where another party has local code execution — no hardened runtime means the process is injectable and the microphone grant is inheritable.
6. **MUST NOT** be used on a machine handling classified or export-controlled material while core dumps are enabled (SEC-09).
7. **MUST NOT** carry the 2026-08-10 risk acceptances into Phase 4. They are scoped to a setting that Phase 4 dissolves, and nothing enforces the scope.
8. **MUST NOT** cite the current `2026-08-10_threat-model-validation.md` as assurance evidence until its false statements are corrected (§8.1).

---

## 10. Minimum Viable Security

### Tier 0 — Correct the record (documentation only; do before anything else)
1. Correct the three false statements in the threat-model validation (§8.1): the "test pins every structural row" claim, the TM-015 file attribution, and the "ASan/UBSan/TSan in CI" claim.
2. Re-rate TM-014, TM-018, TM-011, and TM-012 from "Pass" to "Fail" or "Partial" per SEC-01/02/03/04.
3. Fix `SECURITY.md`: the `localDeckDropPathFrom` function name, and the "enforced structurally" framing.
4. Give the libpng leak (SEC-04) its own tracked bug ID and add it to the accepted-risk table, or fix it.
5. Add an expiry to the accepted-risk table tied to Phase 4 entry and to first third-party-deck use (§7).

### Tier 1 — Required before ANY use with a third-party deck
6. **Fix SEC-02** — range-check `cx`/`cy` at parse time; checked arithmetic in `measureComplexity`.
7. **Fix SEC-01** — cache decoded images; enforce a decode-time budget rather than a frame-geometry estimate.
8. **Fix SEC-03** — single cached part accessor with a byte budget charged on miss; add a cancellation token to `DeckLoader::load`.
9. **Run a fuzzing campaign.** libFuzzer or AFL++ over `DeckLoader::load`, seeded from `tests/fixtures/*.pptx`, under ASan+UBSan, minimum 24h. This was recommended by the project's own testers and never done. SEC-02 is evidence of what one afternoon would return.
10. **Add ASan+UBSan to CI** as a blocking job. This is already claimed and is the cheapest high-value control available.
11. **Add compiler hardening**: `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -D_GLIBCXX_ASSERTIONS`, and `-Wall -Wextra -Werror` on product targets.

### Tier 2 — Required before ANY distribution (Phase 4 entry conditions)
12. **Developer-ID signature + notarization + `--options runtime`**, with library validation left enabled and no entitlement disabling it. Closes TM-003, TM-022, TM-023, and SEC-08 together.
13. **Real dependency vulnerability scanning.** Add CPEs to `sbom.json`; run `osv-scanner --sbom sbom.json` or Grype as a **blocking** CI step; delete the vacuous echo step.
14. **Replace or supplement Semgrep** for C++ with something that can see the language — CodeQL (`cpp` pack) or `clang --analyze`. Stop citing `p/owasp-top-ten` as C++ SAST coverage.
15. **Suppress crash artifacts**: `setrlimit(RLIMIT_CORE, {0,0})` in `main()`.
16. **CI assertion that `MA_NO_DECODING` remains defined.**
17. **Fix `release.yml`** so a signed, checksummed artifact can be produced at all.
18. Remove the `PPTV_VOSK_MODEL_DIR` build-tree fallback from release builds.

### Tier 3 — Required for organizational deployment
19. **Independent reviewer with merge authority.** Enable branch protection. Author = approver = risk acceptor is a SOX ITGC failure and it undermines every acceptance in the project.
20. **Documented vulnerability-response process** — named owner, cadence, and a defined path to reach installed instances.
21. **`docs/INCIDENT_RESPONSE.md`** (already a Phase 4 deliverable).
22. **Written scope exclusion** in `SECURITY.md` prohibiting regulated data, stated as a design consequence of TM-011.

---

## 11. Overall Security Rating

# ⚠️ CONDITIONALLY APPROVED

**Approved for:** the single-presenter, own-machine, own-deck scenario the 2026-08-10 risk acceptances were written for.

**NOT approved for:** entry into Phase 4 distribution, organizational deployment, use with third-party decks, or any environment handling regulated data — until Tier 0 and Tier 1 are complete, and Tier 2 before any distribution.

### Justification

I want to give this project credit where it has earned it, because it has earned a lot. The containment architecture is real and I verified it rather than taking it on faith: no network code, no write-path, no process execution, no logging, no IPC — and speech that genuinely goes from decoder to command gate and nowhere else. The dependency hashes match to the byte. The microphone consent string makes a security claim the code actually honours, which is rarer than it should be. The `[unk]` verification against the *emitted JSON* rather than the input phrases shows someone thinking about the failure mode rather than the happy path. Most tellingly, `SECURITY.md` withdraws a previous claim as false and says so in bold. Projects that do that are projects I trust more, not less.

But this review is a gate, and three things prevent approval to pass it.

**First, the gate artifact is not reliable.** I found three false statements of fact in the threat-model validation and falsified three of its "Pass" ratings from the source — including TM-018, which the report itself calls the highest-severity live-presentation threat, and TM-014, which is contradicted by a finding the project's own testers made and then deferred. A gate is only as good as the evidence beneath it, and this evidence overstates. Correcting it is Tier 0 and costs nothing but honesty, of which this project has already demonstrated an ample supply.

**Second, the assurance apparatus is thinner than the paperwork.** No dependency has ever been CVE-scanned, by any tool, ever — the CI step is disabled by a missing manifest, defers to a Phase 3 Snyk run that is recorded as unauthenticated, and the SBOM's generic purls would match nothing even if it ran. The only real SAST cannot see C++, as the project's own positive control demonstrated. The sanitizers claimed "in CI" are not in CI. And TM-021 — memory safety in a hand-written parser of untrusted archives and untrusted XML, rated High, accepted with no fuzzing — is the one the author flagged as weakest, correctly. I found a fresh signed-overflow UB in that exact input path in about an hour of reading. That is not a criticism of the author's judgment; it is confirmation of it.

**Third, the scope that makes the acceptances reasonable is enforced by nothing at all.** "His own machine and that talk" is a sound basis for accepting eight residuals. It is also a sentence in a Markdown file. No flag, no expiry, no runtime check, no distribution control implements it — and Phase 4 is the precise act that dissolves the premise. Approving passage to Phase 4 on acceptances scoped to pre-Phase-4 conditions would be circular.

None of the technical findings is a remote-code-execution proof, and I am not claiming one. SEC-01, SEC-02, and SEC-03 are denial-of-service and undefined behaviour on the live presentation path; SEC-04 is a low-volume disclosure into an OS log. Individually, each is a few hours of work. What moves this to Conditionally Approved rather than Approved is not their individual severity — it is that all four sit in territory the gate artifact certified as validated, which means the validation process did not catch what one careful reader could. The remedy is proportionate: correct the record, fix three bounded defects, run the fuzzing campaign that was already recommended internally, and turn on the sanitizers that were already claimed. That is a small amount of work standing between this project and a clean approval, and the engineering judgment already demonstrated here is more than sufficient to do it.

**Re-review required** on completion of Tier 0 and Tier 1. Tier 2 must be evidenced before any Phase 4 distribution step executes.

---

### TL;DR (plain English)

This is a voice-controlled slideshow app that runs entirely on one Mac — no internet, no saved files, nothing recorded. I checked those promises against the actual code instead of trusting the documentation, and the big ones are true: it really doesn't touch the network, really doesn't write files, and really does throw away what it hears the instant it decides whether you said "next slide."

The problem is the project's own security sign-off document claims more than the code delivers. It marks several threats "handled" that I was able to break by reading the source. The worst one: there's a safety limit meant to stop a booby-trapped PowerPoint file from freezing the projector mid-talk, but it measures the *size of the picture frame on the slide* instead of the size of the actual image. Shrink the frames to a speck and the limit reads zero while the app grinds away decoding two thousand huge photos. There's also a math bug on the same line that breaks if a file declares an absurdly large picture, and a way to make the app chew through 38 gigabytes of data while its "max 1 gigabyte" guard thinks it's seen 128 megabytes — with no way to cancel.

Separately, a graphics library the app relies on prints little snippets of picture data to the system log when it hits a broken image. Someone already found and reported this months ago, it was never fixed or even given a bug number, and the sign-off document still says that whole category passed.

Two housekeeping gaps matter more than they sound: nothing has *ever* automatically checked the app's building blocks (Qt, libzip, and friends) for known published security holes — the check is switched off in one place and skipped in another — and the app is only "ad-hoc signed," which on a Mac means another program on the same computer could inject itself into it and quietly borrow its microphone permission.

Finally: the owner accepted eight known risks on the reasonable grounds that this was one person, one laptop, one talk, with a file he wrote himself. That was a fair call. But nothing in the software actually holds it to that — copy it to another machine or open a stranger's file and every one of those risks comes back at full strength with no warning light. And the next project phase is *distribution*, which is exactly the thing that breaks that assumption.

**Verdict: conditionally approved.** Fine for the one talk it was built for. Not ready to hand to anyone else until the sign-off document is corrected, three specific bugs are fixed, someone runs an automated stress-test against the file reader, and the app gets properly signed. That's maybe a week of work, and the person who built this is clearly capable of it — the code is genuinely careful, the paperwork just got ahead of it.
