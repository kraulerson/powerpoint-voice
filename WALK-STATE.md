# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-03 (end of session 1; after F4, before F2/F3 voice)
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

## 4. Current position (as of end of session 1)

- **Phase 2 (Construction), current_phase=2.** Phases 0 & 1 gate-approved by Karl.
- **Features built (all through the full Build Loop, tests-first, security-audited):**
  - **F1a DeckLoader::load()** — untrusted .pptx → in-memory slide model (libzip+pugixml).
  - **F1b SlideRenderer::render()** — slide model → QImage (QTextLayout text, images, placeholders).
  - **F4 parseSlideNumber()** — "go to slide N" text → int, fails safe on garbage/overflow.
  - The renderer handles REAL decks: theme/scheme colors, master-txStyles font-size inheritance,
    slideLayout placeholder position, group recursion, wrap, multi-run color, line breaks,
    bullets, luminance-readable-text default, aspect-preserved images.
  - **66 tests green** on macOS + Ubuntu CI; ASan+UBSan clean; semgrep 0.
- **UAT session 1 done** (found 7 real-deck bugs → all fixed) + real-deck remediation round
  (BUG-8 font-size, BUG-10 aspect, EMF→PNG tool). Karl confirmed his real deck renders readably
  (2 EMF images blank due to LibreOffice conversion limits — Karl to re-export in PowerPoint).
- **test-gate:** F4 recorded, **1/2 features to UAT session 2** (the NEXT feature triggers it).
- **PR #11 (F4)** was open at session end; Karl is merging it, then compacting, then resuming.
  On resume: `git checkout main && git pull` (F4 should be merged); if PR #11 not yet merged,
  it's on branch `walk/f4-numbers`.

## 5. NEXT (in order) — resume here

0. **Sync:** `git checkout main && git pull`; delete merged `walk/f4-numbers`. Append a
   `WALK-UNBLOCK-AUDIT.md` row for the PR #11 merge (if not already). Run the session-start
   obligation `bash scripts/check-versions.sh` (project CLAUDE.md) and report.
1. **Build Loop feature F2/F3 — VOICE RECOGNITION (the big one).** `--start-feature`.
   - F2 = voice nav ("next slide"/"previous slide"); F3 = recognition control
     ("pause presentation"/"continue presentation"). Grammar-CONSTRAINED Vosk recognizer
     limited to the 5 phrases + number words (mitigates audience false-trigger, TM-002/019).
   - **New deps (add when F2 starts):** Vosk (prebuilt lib `libvosk` + model
     `vosk-model-small-en-us-0.15` ~40MB, Apache-2.0, BUNDLED in-app — no download at runtime);
     miniaudio (MIT, single-header) for mic capture. macOS needs `NSMicrophoneUsageDescription`
     in the app Info.plist (TCC mic permission).
   - **Design for testability:** keep the grammar-match + command-dispatch layer PURE and
     unit-testable (feed it recognizer text → assert the emitted Command; reuse F4's
     parseSlideNumber for the number). Wrap the Vosk/mic integration behind an interface; that
     layer is exercised in UAT (real audio), not unit tests.
   - Karl at the test gate for ≥3 assertions (give him step-by-step + options + TLDR — his
     standing request for any input point).
2. **Then, by risk:** F5 transcript overlay + listening-state glyph → F6 keyboard parity (every
   command has a key; independent of the speech engine) → **F7 presentation UI** (wires it all:
   File→Open, LoadReportView for warnings, PRE-RENDER cache OFF the UI thread per Bible §3/TM-018,
   route slide to external display, dark holding-screen exit, quit-confirm).
3. **UAT session 2** fires after F2/F3 (2 features since last test): `--start-uat 2`, dispatch
   agent-testers, generate the session doc, triage WITH Karl, fix Fix-Now test-first, gate.
4. **Phase 2 exit → Phase 3 (Validation):** requires no open SEV-1/2, all MVP features, CI green.
   Then the Phase 3 five-scanner gate (`run-phase3-validation.sh`), six-reviewer eval
   (`evaluation-prompts/Projects/run-reviews.sh desktop-app`), Karl's auditor sign-offs
   (security / legal / UAT / pen-test), dual Application-Owner + IT-Security 3→4 approval.
5. **Phase 4 (Release):** author the C++ macOS build/sign steps in `release.yml` (WALK ISSUE-003/010,
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
