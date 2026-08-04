# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-04 (session 2; UAT-2 done, RESEQUENCED to F7 UI -> F6 keyboard -> voice)
**A fresh session (e.g. post-/compact) must be able to continue from THIS FILE ALONE.**
**To resume: read this file top-to-bottom, then `git -C <project> log --oneline -5` and
`bash scripts/process-checklist.sh --status` to confirm live state, then continue at "NEXT".**

---

## 1. What this is

Dogfood walk of the **Solo Orchestrator framework** at maximum rigor, building a REAL app:
**powerpoint-voice**, a voice-controlled offline presentation controller for Karl's live
executive presentation (~week of 2026-08-10). Dual purpose: ship the app AND stress-test the
framework's most rigorous path, logging every stumble.

**Roles & rules (do not soften):**
- Karl Raulerson plays EVERY human approver/reviewer/tester slot. The orchestrator (Claude,
  junior-dev persona) STOPS at every human decision — never self-attests, never simulates
  Karl's approval, presents evidence per item before asking.
- Framework bugs are FINDINGS (→ `WALK-ISSUE-LOG.md`), NEVER fix targets. Never edit any file
  inside the `../solo-orchestrator/` clone (one documented exception: the NEW file
  `templates/pipelines/ci/github/cpp.yml`).
- No enforcement bypasses ever (no `--no-verify`, `--admin`, `--force` past a gate). Escape
  hatches only where documented, every use logged.
- Never conclude from truncated output; never pipe `git commit` (verify with `git log -1`).

## 2. Fixed configuration

- Framework clone: `../solo-orchestrator` @ `6417a25` (HTTPS).
- **deployment=organizational · gov_mode=production (NO POC) · track=full · enforcement=strict**
- platform=desktop · language=cpp · host=github.
- Repo `kraulerson/powerpoint-voice` — **PUBLIC**, org-mode branch protection LIVE (main takes
  PRs only, 1 required review, enforce_admins on).
- **Architecture (ADR-0001, Karl-selected):** C++20, **Qt 6** (from-scratch OOXML renderer),
  **Vosk** on-device speech, libzip+pugixml parse, CMake+Ninja. Fully offline; no auth/network.

## 3. Standing protocols (LEARNED — keep applying)

1. **Merges to main:** agent prepares PR + green CI on BOTH platforms, then STOPS. Karl
   approves/merges via his `kraulerson-reviewer` account. Every merge → a row in
   `WALK-UNBLOCK-AUDIT.md` (resolves ISSUE-006: solo org-mode can't self-merge).
2. **Recorder identity (resolves ISSUE-008):** repo git identity is `kraulerson-reviewer`
   (set repo-locally). The recorder commits what approver **Karl Raulerson** decides. NEVER
   author an APPROVAL_LOG approver row as "Karl Raulerson" — the self-approval verifier blocks
   every solo arm. Approver ROWS name Karl; commit AUTHOR is kraulerson-reviewer.
3. **ZDR / Confidential deck (intake §5.1.1):** the real deck
   `../Solo Orchestrator - FirstService IT Summit.pptx` is Confidential, lives OUTSIDE the
   repo, is NEVER committed (public repo), and its CONTENT is never sent to the LLM. Render it
   locally for fidelity checks but **do NOT open/Read the rendered PNGs** — Karl is the eyes.
   Reading structure/metadata (slide counts, element types, attribute values, file sizes) is OK.
4. **Project CLAUDE.md is binding:** phase-entry commands own `current_phase`; docs-only commits
   bypass the Build Loop gate; `feat:` needs an active `--start-feature`; `.claude/test-command`
   → `scripts/run-tests.sh` runs the full suite at every source commit.
5. Product truth = `PROJECT_INTAKE.md` + `PRODUCT_MANIFESTO.md`. Command grammar is the five
   TWO-word phrases: **"next slide" / "previous slide" / "pause presentation" /
   "continue presentation" / "go to slide N"** (Karl's Q1 change; supersedes intake single words).

## 4. Current position (session 2, 2026-08-04)

- **Phase 2 (Construction), current_phase=2.** Phases 0 & 1 gate-approved by Karl.
- **Features built (all through the full Build Loop, tests-first, security-audited):**
  - **F1a DeckLoader::load()** — untrusted .pptx → in-memory slide model (libzip+pugixml).
  - **F1b SlideRenderer::render()** — slide model → QImage (QTextLayout text, images, placeholders).
    Handles REAL decks: theme colors, master-txStyles font inheritance, layout placeholders,
    group recursion, wrap, multi-run color, line breaks, bullets, aspect-preserved images.
  - **F4 parseSlideNumber()** — "go to slide N" text → int, fails safe on garbage/overflow.
  - **F2/F3 voice-command grammar & dispatch** (PR #12, merged 2e3ad38) — `matchCommand()`
    (phrase-level closed grammar, no false-trigger) + `RecognizerController` (Active/Paused
    dispatch gate; Paused drops nav for Q&A). Pure/unit-tested; engine plugs in behind
    `IRecognizer`. 2-agent audit fixed 1 High (sink-exception mid-talk crash) + 2 Med test-first.
  - **90 tests green** on macOS + Ubuntu CI; ASan+UBSan clean; semgrep 0.
  - **UAT session 2 done** (branch walk/uat-2, PR #13, merged 05823ff): 3 agent-testers confirmed
    the safety property SOLID (could not force a false command or crash). Fixed BUG-11 (SEV-1
    stuck-in-Paused → resume synonyms) + BUG-12 (SEV-2 natural filler tolerance) test-first per
    Karl triage Option A; BUG-13..16 (SEV-3: directional aliases, spoken-number naturalness,
    unicode punctuation, out-of-range clamp) DEFERRED to the voice-engine feature / F7.
    `tools/command_probe` added (typed phrase → command). No open SEV-1/2.
- **UAT session 1 done** (7 real-deck bugs fixed) + real-deck remediation (BUG-8 font-size,
  BUG-10 aspect, EMF→PNG tool). Karl confirmed his real deck renders readably (2 EMF images
  blank due to LibreOffice conversion limits — Karl to re-export in PowerPoint).
- **test-gate: counter reset after UAT-2; clear to continue (2 features until next UAT).**
- **SCOPE SPLIT (Karl-approved):** the Vosk speech engine + mic capture is a SEPARATE feature,
  NOT part of F2/F3. Its deps are already vendored (git-LFS, pinned + SHA-256); it is now
  RESEQUENCED to run after F7/F6/UAT-3 — see NEXT.
- **BUG-17 fixed (design review):** bare "pause"/"continue"/"resume" no longer match; every command
  requires its object, closing a one-word audience un-pause during Q&A (TM-002/019). 93 tests green.
- Local commit gate (`scripts/run-tests.sh`) now ALSO runs clang-format (S-25 gap closed).

## 5. NEXT (in order) — resume here

> **RESEQUENCED 2026-08-04 (Karl decision).** An 8-agent pre-implementation design review of the
> voice engine found that every voice-failure path ended in a keyboard fallback that did not exist —
> and that the app had **no presentation UI at all** (dark StartView only) with 6 days to the talk
> (BUG-18). Four features were built to production rigor without the product ever being usable.
> **New order: F7 presentation UI -> F6 keyboard parity -> UAT-3 (Karl drives his REAL deck by
> keyboard) -> voice engine.** Rationale: guarantee a working presenter first; voice then becomes an
> enhancement with a real fallback behind it, not a single point of failure.
> The voice design + all critic findings are preserved in `docs/design-notes/voice-engine-design.md`.

1. **F7 PRESENTATION UI — CURRENT Build Loop (`F7-presentation-ui`, branch `walk/voice-engine`).**
   Wire the built library into a usable presenter: File->Open a .pptx, LoadReportView for warnings,
   **PRE-RENDER the deck off the UI thread** (Bible §3 / TM-018 — never render lazily mid-talk),
   fullscreen slide display, route to the external display, dark holding-screen exit, quit-confirm.
   **Must range-clamp slide numbers (BUG-16)** — the matcher deliberately does not.
2. **F6 KEYBOARD PARITY.** All five commands on keys, routed through the SAME matchCommand ->
   RecognizerController dispatch path the voice engine will use (so keyboard and voice share one
   code path, and the keyboard is the audited fallback). Covers the residual stuck-in-Paused risk
   left by BUG-17.
3. **UAT session 3** (2 features since UAT-2): Karl presents his REAL deck with the keyboard.
4. **VOICE-ENGINE FEATURE (deferred to after UAT-3).** Makes voice actually work: Vosk +
   miniaudio mic capture implementing the audited `IRecognizer` interface.
   **Deps are DONE** — libvosk (macOS universal2 + Linux x86_64), the ~40MB model, miniaudio and
   `vosk_api.h` are vendored via git-LFS, pinned + SHA-256 verified (`third_party/PROVENANCE.md`).
   Vosk pinned **0.3.44** (0.3.45 ships no macOS build). macOS still needs
   `NSMicrophoneUsageDescription` in the app Info.plist (TCC mic permission).
   **START FROM `docs/design-notes/voice-engine-design.md`** — the full design PLUS the three
   critics' NEEDS_CHANGES findings, which must be folded in BEFORE implementation. Highest-value:
   `vosk_recognizer_set_grm` is NOT exported by the 0.3.44 macOS lib (builds green on Ubuntu CI,
   fails on the showtime Mac); a model without the expected layout makes the grammar constraint
   silently degrade to a full ~200k-word decoder (destroying the false-trigger defense); invalid
   grammar JSON SEGFAULTS rather than returning null; out-of-vocabulary grammar tokens are silently
   dropped (a dropped "resume" = stuck in Paused); `assert()` is a no-op in this forced-Release
   build. MUST honor the `IRecognizer` contract: finalized-phrases-only + same-thread delivery.
   Also address deferred BUG-13/14 (directional aliases, spoken-number naturalness).
5. **F5 transcript overlay** + listening-state glyph + pre-show voice check (final MVP item).
6. **Phase 2 exit → Phase 3 (Validation):** requires no open SEV-1/2, all MVP features, CI green.
   Then the Phase 3 five-scanner gate (`run-phase3-validation.sh`), six-reviewer eval
   (`evaluation-prompts/Projects/run-reviews.sh desktop-app`), Karl's auditor sign-offs
   (security / legal / UAT / pen-test), dual Application-Owner + IT-Security 3→4 approval.
7. **Phase 4 (Release):** author the C++ macOS build/sign steps in `release.yml` (WALK ISSUE-003/010,
   deferred to here), rollback test, go-live smoke test, HANDOFF.md, tag **v1.0.0**. Walk DONE.

## 6. Build / run recipe (macOS local — REQUIRED env)

```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
(cd build && ctest --output-on-failure)   # 66 tests
```
- Full test command (what the commit gate runs): `bash scripts/run-tests.sh`.
- Render a deck to PNGs (fidelity check): `bash scripts/render-deck.sh <deck.pptx>` → `render-out/`.
- Convert a deck's EMF images to PNG: `bash scripts/convert-deck-emf.sh <in.pptx> <out.pptx>`.
- CI ubuntu deps: `.github/ci-deps-apt.txt` (qt6-base-dev, libgl1-mesa-dev, pkg-config,
  libzip-dev, libpugixml-dev, fonts-dejavu-core).

## 7. Toolchain installed locally

cmake 4.4.2, ninja, llvm/clang-format/clang-tidy 22, Qt 6.11.1 (brew), libzip 1.11.4,
pugixml 1.16, pkgconf 3.0.5, LibreOffice (soffice, for EMF conversion), gh (as kraulerson +
kraulerson-reviewer), semgrep/gitleaks/snyk/docker.

## 8. Companion files (all committed)

- `WALK-ISSUE-LOG.md` — append-only findings (ISSUE-001…012 + OBSERVATION-005/009 + S-1…S-21
  smooth notes). Framework findings incl. ISSUE-002 (generate_ci ships other.yml for a
  discovered language), ISSUE-006/008 (solo org-mode blockers, resolved), ISSUE-007 (gate
  deadlock), ISSUE-010 (invalid release.yml), ISSUE-011 (C++ lockfile gap).
- `WALK-UNBLOCK-AUDIT.md` — every Karl-executed merge/un-block (rows 1–8 so far).
- `BUGS.md` — BUG-1…10 (all Fixed or Won't-Fix-MVP). No open SEV-1/2.
- `APPROVAL_LOG.md` — 6 org pre-conditions + Phase 0→1 (Sponsor) + Phase 1→2 (STA) approvals.
- Memory pointer: `~/.claude/projects/-Users-karl-Documents-Claude-Projects-powerpoint-voice/memory/powerpoint-voice-walk.md`.

## 9. Findings tally (for WALK-REPORT.md at the end)

12 issues + 2 observations + 21 smooth notes. Blocker-class resolved: ISSUE-006 (un-block
protocol), ISSUE-008 (recorder identity). The framework's per-feature security audits + UAT
have EACH caught real ship-blocking bugs (F1a: 3 Crit+1 High; F1b: 1 Crit+2 High; UAT-1:
2 SEV-1 incl. invisible-text; F4: overflow/wrong-jump) — strongest evidence the rigor works.
Time: ~session 1 was long (interview → F4). Multi-session expected.
