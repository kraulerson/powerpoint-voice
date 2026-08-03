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

**PHASE 1 ENTERED (current_phase=1, gate exit 0, snapshot phase-0-to-1_2026-08-03). Awaiting Karl's merge of PR #4 (branch walk/phase0); then Phase 1 architecture work begins: 3 architecture options → Karl selects → STRIDE threat model → data model → UI scaffold spec → Project Bible → Senior Technical Authority gate. Phase 0 complete: Manifesto approved (Sponsor, 2026-08-03), ISSUE-007/008 logged and resolved (warn-knob single use; recorder identity).**

Done (session 1):
1. Product interview + 7 interactive judgment decisions → PROJECT_INTAKE.md fully filled
   (Manual mode; 0 blank cells; `scripts/resume.sh` now prints the §13 init prompt —
   state machine agrees intake is done, Phase 0 not started).
2. Framework cloned @6417a25; README + full User Guide read; cpp.yml extension authored.
3. Init run (org/production/full/cpp/desktop): project + repo + hooks + 83/83 verify checks.
4. Findings logged: ISSUE-001 (doc path), **ISSUE-002 Major** (generate_ci ships other.yml
   for discovered language — worked around by copying cpp.yml into project ci.yml; validated
   green in CI), ISSUE-003 (release.yml TODO null-steps for cpp — Phase 4 item),
   ISSUE-004 (org+free-tier protection dead-end — resolved: repo public, protection live),
   OBSERVATION-005 (dual-hatted backup maintainer), **ISSUE-006 Blocker** (solo org-mode
   unmergeable main — resolved by Karl's standing un-block protocol + WALK-UNBLOCK-AUDIT.md).
5. Governance: 6 org pre-conditions APPROVED by Karl (APPROVAL_LOG rows 1-6; ITSM = issue #1);
   PR #2 merged by Karl (audit rows 1-2).
6. Data governance: classification=confidential, zdr_attested=false, exception wording
   approved verbatim by Karl (intake §5.1.1) — needed for the Phase 1→2 gate.

## NEXT (in order)

1. **KARL (open):** review PROJECT_INTAKE.md on the `walk/intake` PR — especially §6.1
   (technical profile: DRAFTED from context, flagged for his correction), §2 (problem/
   success criteria as written for him), §4.1 (the 7 must-haves) — then approve/merge
   (append audit row on merge).
2. Run session-start obligation: `bash scripts/check-versions.sh` (project CLAUDE.md
   requires it; report results before phase work).
3. **Phase 0 kickoff:** use the §13 prompt (printed by `bash scripts/resume.sh`) with the
   Builder's Guide (`docs/reference/builders-guide.md`) + desktop Platform Module
   (`docs/platform-modules/desktop.md`). Follow Builder's Guide Phase 0 steps 0.1-0.4 with
   the "With Intake" prompts; personas per CLAUDE.md table (0.2 = Skeptical PM).
4. Phase 0 output: PRODUCT_MANIFESTO.md → present evidence to Karl → his Sponsor approval
   → append Phase 0→1 gate entry to APPROVAL_LOG.md → `scripts/check-phase-gate.sh` →
   `scripts/process-checklist.sh --start-phase1`.
5. Also pending: trademark search (Standard+ Phase 0 duty) — do during Phase 0.

## Session practicalities

- NEVER pipe git commit; verify every commit with `git log -1`. Docs-only commits pass the
  gate; source commits need the Build Loop.
- Init log: `.solo-orchestrator/init-20260803-072618.log`. Walk memory pointer:
  `~/.claude/projects/.../memory/powerpoint-voice-walk.md`.
- Time spent session 1 so far: ~1h05m wall clock (interview → intake complete).
