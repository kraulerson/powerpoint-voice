# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-03 ~10:05 MDT (session 1)
**A fresh session must be able to continue from this file alone. Karl says "continue".**

## What this is

Dogfood walk of the Solo Orchestrator framework at maximum rigor, building a REAL app:
a voice-controlled presentation app for Karl's live executive presentation (~2026-08-10).
Karl plays EVERY human approver slot; the orchestrator (Claude, junior-dev persona) STOPS
at every human decision — never self-attests, never simulates approval, presents evidence
per item. Framework bugs are FINDINGS (→ WALK-ISSUE-LOG.md), never fix targets; no
enforcement bypasses ever; escape hatches only where documented and always logged.

## Fixed configuration (do not soften)

- Framework clone: `../solo-orchestrator` @ `6417a25` (HTTPS, 2026-08-03)
- deployment=organizational · gov_mode=production (NO POC) · track=full · enforcement=strict
- platform=desktop · language=cpp (extension: `templates/pipelines/ci/github/cpp.yml`
  authored as NEW file in clone — documented extension point; nothing else touched)
- host=github · repo `kraulerson/powerpoint-voice` — now PUBLIC (Karl's decision resolving
  ISSUE-004 via the driver's documented option 2), org-mode branch protection LIVE and
  preflight-verified: main takes PRs only, 1 required review, enforce_admins on.

## Standing protocols

1. **Merges to main:** agent prepares PR + green CI, STOPS; Karl approves/merges via his
   `kraulerson-reviewer` account; every un-block appended to WALK-UNBLOCK-AUDIT.md (resolves
   ISSUE-006). Batch bookkeeping commits into milestone PRs to respect his time.
1b. **Recorder identity (resolves ISSUE-008):** repo git identity is `kraulerson-reviewer`
   (user.name/email set repo-locally) — the recorder persona commits what approver Karl
   Raulerson decides. NEVER commit an APPROVAL_LOG approver row under author "Karl Raulerson"
   (the self-approval verifier blocks every solo arm; see ISSUE-008). All future gates use
   this convention.
2. **Project CLAUDE.md is binding** (session version check, phase-entry commands own
   `current_phase`, pending-approval sentinel for structured decisions, escalate-to-user
   instead of bypass suggestions, docs-only commits bypass Build Loop gate).
3. Product decisions from the interview + Karl's 7 intake judgment answers are recorded in
   PROJECT_INTAKE.md — that file is now the single product-truth source (supersedes the
   interview capture that used to live in this file).

## Current position

**PHASE 2 ENTERED (current_phase=2, both gate dates recorded, gate exit 0, snapshot
phase-1-to-2_2026-08-03). Scaffold committed (e058e52 on branch `walk/phase1`), NOT yet
merged to main — awaiting Karl's merge of the Phase-1+scaffold gate PR. Next: begin the
Build Loop, feature F1 (PPTX load & render) first per risk order.**

Phases 0, 1 COMPLETE and gate-approved by Karl (Sponsor 0→1; Senior Technical Authority 1→2).
Architecture: ADR-0001 Qt 6 + Vosk on a from-scratch OOXML renderer. Local toolchain now
installed (cmake 4.4.2, ninja, llvm/clang-format/tidy 22, Qt 6.11.1 via brew). Scaffold
builds + ctest 1/1 green + launches headless.

Done (session 1):
1. Interview + 7 intake decisions → PROJECT_INTAKE.md complete (merged PR #3).
2. Framework cloned @6417a25; cpp.yml extension authored; init run (org/production/full/cpp/desktop).
3. Phase 0: FRD + user-journey + data-contract (parallel agents) → PRODUCT_MANIFESTO.md,
   Q1-Q13 resolved (incl. grammar → two-word phrases), Sponsor-approved (merged PR #5).
4. Phase 1: ADR-0001 (Karl selected Qt6+Vosk), threat-model.md (23 STRIDE, TM-IDs),
   data-model.md, ui-scaffolding.md, PROJECT_BIBLE.md (16 §). ZDR gate satisfied. STA-approved.
5. Phase 2 init: installed toolchain; authored buildable Qt6 scaffold (CMake + doctest);
   7/7 init verified; current_phase→2.
6. Findings 001-011 logged (see WALK-ISSUE-LOG.md). Resolved blockers: ISSUE-006 (un-block
   protocol), ISSUE-008 (recorder identity). Standing conventions in "Standing protocols" above.

## Repo state (F1 + UAT-1 done; F4 done — awaiting merge)

- `main` @ merged through PR #10 (F1 + UAT-1 + real-deck remediation). current_phase=2.
- Branch **`walk/f4-numbers`** (unmerged, PR to open): **feature F4 (slide-number parser) complete**
  through the full Build Loop — parseSlideNumber(). 66 tests green. Karl merges next.
- test-gate: F4 recorded (1/2 to UAT session 2; the NEXT feature triggers it).
- Branch **`walk/uat-1b`** (unmerged, to PR): real-deck remediation from Karl's actual deck
  render — **BUG-8** (font-size inheritance from master txStyles — was blank/tiny text; FIXED),
  **BUG-10** (image aspect preserved — no squish; FIXED), **BUG-9** (EMF/WDP images = Won't-Fix-MVP,
  addressed via the convert-deck-emf tool below). 50 tests green.
- Real deck: `../Solo Orchestrator - FirstService IT Summit.pptx` (Confidential, OUTSIDE repo,
  never commit). Karl's report: text now readable after BUG-8; EMF images were placeholders.
- **Tools:** `scripts/render-deck.sh <deck>` (render to render-out/); `scripts/convert-deck-emf.sh
  <in> <out>` (LibreOffice-backed EMF/WDP→PNG repackage — LibreOffice cask now installed).
- Toolchain local: cmake 4.4.2, ninja, llvm 22, Qt 6.11.1, libzip 1.11.4, pugixml 1.16,
  pkgconf 3.0.5, LibreOffice (soffice). `.claude/test-command` → `scripts/run-tests.sh` (50 tests).

## What F1 + UAT delivered (renderer handles REAL decks)

- **DeckLoader::load()** — untrusted .pptx → slide model: theme/scheme colors, master txStyles
  font-size inheritance, slideLayout placeholder position, group recursion, line breaks, bullets.
- **SlideRenderer::render()** — QImage via QTextLayout (wrap, per-run color/font, breaks, bullets),
  luminance-based readable-text default, aspect-preserved images, visible placeholders.
- **50 tests green** + ASan/UBSan clean. Karl's real deck (10 slides) renders readably.
- Known limits: EMF/WDP images (use convert-deck-emf), theme1/default clrMap, one-level layout
  inheritance, no group transform, inline-only bullets.

## NEXT (in order)

0. **KARL:** re-check the converted deck render (EMF→PNG); confirm real-deck fidelity acceptable.
   Then merge the walk/uat-1b PR (as kraulerson-reviewer); sync main; delete branch; audit row.
1. **Build Loop feature F4 — "go to slide N" number parsing** (next feature, `--start-feature`).
   Highest-value non-render feature: normalize "fifteen"/"one five"/"15" → int, range-check
   against deck length. Pure/testable; no new deps. Karl at the test gate for ≥3 assertions.
2. Then by risk: F2/F3 voice nav+control (add Vosk + miniaudio deps when F2 starts; grammar =
   the 5 two-word phrases) → F5 transcript overlay + listening glyph → F6 keyboard parity →
   F7 presentation UI (wires it together: File→Open, LoadReportView for warnings, pre-render
   cache OFF the UI thread per Bible §3/TM-018, route to external display, dark holding exit).
   UAT session again after every 2 features (F4+F2 → session 2, etc.).
3. Phase 2 exit → Phase 3 (validation): needs NO open SEV-1/2 (currently clean), all MVP
   features, CI green. Then Phase 3 five-scanner gate, six-reviewer eval, Karl's auditor
   sign-offs (security/legal/UAT/pen-test), dual 3→4 approval, Phase 4 release to v1.0.0.

## Build / run recipe (macOS local)

```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest --output-on-failure)
# headless app smoke: QT_QPA_PLATFORM=offscreen build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice
```
CI deps for ubuntu are in `.github/ci-deps-apt.txt` (qt6-base-dev, libgl1-mesa-dev).

## Session practicalities

- NEVER pipe git commit; verify every commit with `git log -1`. Docs-only commits pass the
  gate; source/feature commits need an active Build Loop. `feat:` requires `--start-feature`.
- Repo git identity = `kraulerson-reviewer` (recorder); approver rows name Karl Raulerson.
- Init log: `.solo-orchestrator/init-20260803-072618.log`. Walk memory pointer:
  `~/.claude/projects/.../memory/powerpoint-voice-walk.md`.
- Time spent session 1: ~2h15m wall clock (interview → Phase 2 scaffold + green cross-platform CI).
- Findings tally at checkpoint: 11 issues (ISSUE-001…011) + 2 observations, 12 smooth notes.
  Blocker-class resolved: ISSUE-006 (un-block protocol), ISSUE-008 (recorder identity).
  Major open-as-findings (framework, not fix targets): ISSUE-002 (generate_ci ships other.yml),
  ISSUE-007 (gate deadlock), ISSUE-010 (invalid release.yml), ISSUE-008. Deferred project work:
  ISSUE-003/010 (release.yml C++ steps → Phase 4).
