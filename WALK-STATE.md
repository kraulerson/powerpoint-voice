# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-03 ~07:40 MDT (session 1)
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
  authored as NEW file in clone — the documented extension point; nothing else touched)
- git host=github · repo `kraulerson/powerpoint-voice` (private — forced by org mode) ·
  project dir `/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice`

## Product decisions from the Karl interview (feed intake; already confirmed)

- Commands v1: baseline five ONLY — next, previous, pause, continue, "go to slide N"
- Input: .pptx loaded directly; **from-scratch C++ native renderer** (Karl's explicit choice
  over embedded-LibreOffice recommendation — schedule risk to be formally logged in intake §11)
- Renderer must-support tier: **text + images** (text boxes, placed images, solid/picture
  backgrounds); fonts UNKNOWN → design defensively for embedded/custom fonts
- Reliability (all four): on-device speech (zero network); live transcript overlay; keyboard
  fallback for every command; robust number recognition ("fifteen"/"one five"/"15", range checks)
- UI: minimal dark, slides own the screen. Venue: MacBook + built-in mic + projector, assume no Wi‑Fi
- Priority ladder: P0 = 5 commands + 4 reliability + pptx load/render + dark UI on macOS;
  P1 = timer, practice mode; P2 = presenter notes, Win/Linux packaging
- Platforms: C++ portable, CI builds all three; pre-showtime validation macOS-only
- Public repo was Karl's wish → overridden by org-mode forced-private (see ISSUE-004);
  real deck NEVER committed (synthetic + sanitized fixtures only)

## Current position

**Phase: pre-Phase-0. Init done (with findings). STOPPED at a Karl decision.**

Done:
1. Product interview complete (answers above).
2. Framework cloned; README + full User Guide read.
3. `cpp.yml` authored at documented extension point (ISSUE-001 path note).
4. `init.sh --non-interactive` run: project scaffolded, repo created+pushed (private),
   83/83 verify-install checks pass, exit 2 — branch protection failed on free-tier
   private repo (ISSUE-004; framework printed 3 documented options).
5. ISSUE-002 (Major): generated ci.yml was other.yml skeleton, not cpp.yml —
   worked around by copying the cpp template into the PROJECT's .github/workflows/ci.yml.
6. ISSUE-003 (Minor): release.yml language placeholders are TODO null-steps for cpp; deferred to Phase 4.
7. WALK-ISSUE-LOG.md started (4 issues, 4 smooth notes).

## NEXT (in order)

1. **KARL DECISION (open):** branch protection — the three documented options from the
   github driver: (a) GitHub Pro $4/mo, (b) make repo public (matches his interview wish;
   framework driver offers it explicitly), (c) attest manually (`--branch-protection-attested`
   / `scripts/check-gate.sh --repair` flow). Record decision + run repair + verify with
   `scripts/check-gate.sh --preflight`.
2. **KARL SIGN-OFFS:** the 6 organizational pre-conditions (User Guide §1.2) — AI deployment
   path, insurance, liability entity, sponsor, backup maintainer, ITSM. Each presented
   individually with evidence; recorded in APPROVAL_LOG.md / PROJECT_INTAKE.md §8. ALL must
   be Resolved before Phase 0.
3. Post-init auth checks (claude OK; `snyk auth` status to verify).
4. Intake: `bash scripts/intake-wizard.sh` (or non-interactive equivalent), seeded from the
   interview capture above; confirm judgment questions with Karl.
5. `bash scripts/resume.sh` → Phase 0 kickoff per its printed prompt.

## Session practicalities

- Commit walk logs as you go (docs-only commits bypass the Build Loop gate by design —
  .md/.yml staged together qualify). NEVER pipe git commit; verify each with `git log -1`.
- Direct pushes to main currently possible (no protection yet); once protection lands in
  org mode, expect PR + 1 approving review required — plan the workflow accordingly.
- Init log: `.solo-orchestrator/init-20260803-072618.log`.
