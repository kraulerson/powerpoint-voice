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

## Repo state (F1a complete — awaiting merge)

- `main` @ `2de8c29` (PR #6 merged). current_phase=2.
- Branch **`walk/audit-s12`** = PR **#7** (OPEN, CI GREEN both platforms). Holds: session-1
  audit bookkeeping (S-12) + **feature F1a (deck loader) complete through the full Build Loop**
  + the pkg-config CI fix. **Karl merges PR #7 next** (append WALK-UNBLOCK-AUDIT row on merge,
  sync main, delete branch). Build Loop for F1a is CLOSED; F1a recorded (1/2 to next UAT).
- Toolchain now installed locally: cmake 4.4.2, ninja, llvm 22, Qt 6.11.1, libzip 1.11.4,
  pugixml 1.16, pkgconf 3.0.5. Build recipe below UPDATED (needs PKG_CONFIG_PATH).

## What F1a delivered

`DeckLoader::load()` — untrusted .pptx → in-memory slide model (libzip+pugixml). 17 tests
(incl. Karl's 7 gate assertions + 4 security-audit regressions). 5-agent security audit found
& fixed 3 Critical + 1 High test-first (see docs/security-audits/f1a-deck-loader-security-audit.md).
The Manifesto's F1 is HALF done: parsing is F1a; **rendering the slide model to pixels is F1b** (next).

## NEXT (in order)

0. **KARL:** merge PR #7 (as kraulerson-reviewer); sync main; delete branch; audit row.
1. **Build Loop feature F1b — slide RENDER (slide model → QPixmap via QPainter/QTextLayout).** Per CLAUDE.md Build
   Loop: `--start-feature`, tests FIRST (RED) — Karl writes ≥3 assertions at the test gate —
   verify failing, implement (GREEN), security audit (5 parallel agents), docs, `--record-feature`.
   Carry the Bible §3 pre-render-off-thread decision (TM-018) + the F1a-5 non-positive-slide-size
   guard obligation. F1a+F1b together = 2 features → triggers the first UAT session.
2. UAT session after F1b (test-gate.sh --check-batch). SEV-1/2 block Phase 2→3.
3. Remaining feature order by risk: F4 number parsing → F2/F3 voice nav+control (Vosk) → F5
   overlay → F6 keyboard parity → F7 presentation-mode UI. (F6 keyboard is independent of the
   speech engine — good early-integration anchor.) Vosk + miniaudio deps get added when F2 starts.

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
