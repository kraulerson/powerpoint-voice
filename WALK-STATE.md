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

## NEXT (in order)

1. **KARL (open):** merge the Phase-1+scaffold gate PR (`walk/phase1`) as kraulerson-reviewer;
   append WALK-UNBLOCK-AUDIT row on merge; sync main; delete branch.
2. **Build Loop feature F1 — PPTX load & render (highest risk first).** Per CLAUDE.md Build
   Loop: `--start-feature`, tests FIRST (RED) — Karl writes ≥3 assertions at the test gate —
   verify failing, implement (GREEN), security audit (5 parallel agents), docs, `--record-feature`.
   F1 needs new deps: libzip 1.11 + pugixml 1.15 (add via FetchContent when F1 starts).
   Commit type `feat:` is now valid once a Build Loop is active.
3. UAT session after every 2 features (test-gate.sh). SEV-1/2 block Phase 2→3.
4. Feature order by risk: F1 render → F4 number parsing → F2/F3 voice nav+control → F5 overlay
   → F6 keyboard parity → F7 presentation-mode UI. (F6 keyboard is independent of the speech
   engine — good early-integration anchor.)

## Build / run recipe (macOS local)

```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
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
- Time spent session 1: ~2h wall clock (interview → Phase 2 scaffold).
