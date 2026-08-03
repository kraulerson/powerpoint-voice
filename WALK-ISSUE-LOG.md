# WALK-ISSUE-LOG — powerpoint-voice (Solo Orchestrator full-rigor walk)

Append-only. Dogfood walk of solo-orchestrator @ `6417a25` (clone 2026-08-03, HTTPS).
Configuration under test: **deployment=organizational, gov-mode=production, track=full,
enforcement=strict (default), platform=desktop, language=cpp, host=github**.
Human in every approver slot: Karl. Orchestrator: Claude (junior developer persona).

Severity scale: Blocker / Major / Minor / Confusion. Smooth notes logged too.

---

## ISSUE-001 — README language-extension path omits the host directory

- **When/where:** 2026-08-03 ~07:20, pre-init, planning the C++ language extension.
- **Expected (doc):** README "Key Features": *"Need C++? Drop one CI template at
  `templates/pipelines/ci/cpp.yml` — it appears as a language option automatically."*
- **Actual:** `ls templates/pipelines/ci/` → `bitbucket github gitlab` — templates live
  one level deeper, per host. The real drop point is `templates/pipelines/ci/github/cpp.yml`
  (github/ is canonical for discovery per docs/extending-platforms.md, which has it right).
- **Severity:** Minor (doc drift; extending-platforms.md and init.sh:655 agree on the real path).
- **Resolution:** Used the host-scoped path. Authored `cpp.yml` (marker `platforms=desktop`)
  as a **new file** at the documented extension point — no existing framework file edited.
  Deviation noted: only `github/` was populated, not gitlab/bitbucket (spec 2026-04-21 says
  hosts ship the same language set — contributor-facing consistency rule; this walk uses github only).
- **Time lost:** ~3 min.

## ISSUE-002 — Language auto-discovery accepts a new language; generate_ci silently ships other.yml (MAJOR)

- **When/where:** 2026-08-03 07:26, init run; confirmed 07:33 in the generated project.
- **Expected (doc):** README: *"Need C++? Drop one CI template … it appears as a language
  option automatically"* — and by strong implication, the generated project then gets that CI.
  `--validate-only` accepted `--language cpp` for desktop (discovery works: init.sh:655 scans
  `templates/pipelines/ci/github/*.yml` + platforms marker).
- **Actual:** Generated project's `.github/workflows/ci.yml` is **other.yml** — the
  intentionally-failing TODO skeleton. Evidence: `head -5 .github/workflows/ci.yml` →
  marker `platforms=web,desktop,mobile,mcp_server` + *"TODO: Customize these steps… This is
  a skeleton"*. Root cause (read, not guessed): `generate_ci()` (init.sh ~2946) maps language
  → template via a **hardcoded case statement** whose `*)` arm is `other.yml`; it never
  consults the filesystem that discovery scanned. No warning printed at init.
- **Severity:** **Major.** On a Full-track organizational project the Tier-1 CI enforcement
  floor (SAST, tests, audit, license, governance checks) silently became a skeleton. The
  extensibility claim holds at the option level and breaks at the generation level.
- **Known-ledger check:** grepped `solo-orchestrator-backlog.md` and `solo-orchestrator-bugs.md`
  for generate_ci / other.yml / language-extension entries — no match found. Appears NEW.
- **Resolution (minimal workaround, project-side only):** did by hand exactly what
  generate_ci should have done — `cp <clone>/templates/pipelines/ci/github/cpp.yml
  .github/workflows/ci.yml` in the **project** (framework clone untouched). Commit recorded below.
- **Time lost:** ~20 min (code reading to root-cause + workaround).

## ISSUE-003 — get_release_vars: unknown language yields null `uses:` TODO steps in release.yml

- **When/where:** 2026-08-03 07:34, generated project review.
- **Expected (doc):** README: *"Adding a new language requires one file: a CI template."*
  Release pipelines are documented as TODO-bearing (signing/secrets), so some TODOs are by design.
- **Actual:** `get_release_vars()` (init.sh ~2871) is also a hardcoded case; its `*)` arm
  substitutes literal TODO text, producing `- uses: # TODO: Add setup action for your language`
  — a null step. The language-injection half of the release template is silently absent for a
  discovered language: `grep -n TODO .github/workflows/release.yml` → lines 32/37/40 are the
  language placeholders, on top of the by-design signing TODOs.
- **Severity:** Minor (release.yml requires pre-release configuration anyway; but the
  "one file" claim understates what a new language actually gets).
- **Resolution:** Deferred — release pipeline is configured in Phase 4 per the framework;
  will author the C++ build/sign steps then. Logged here so the claim-vs-reality gap is recorded.
- **Time lost:** ~5 min.

## ISSUE-004 — Org mode forces private repo; free-tier GitHub cannot protect private repos → init exits "Setup INCOMPLETE" (works as designed, arrives as a surprise)

- **When/where:** 2026-08-03 07:26-07:27, init `create_and_protect_remote`.
- **Expected (doc):** User Guide §1.2 org accounts: *"GitHub Team or Enterprise — your
  organization's standard."* `--help-non-interactive`: *"organizational deployments force
  private."* Product owner had chosen a **public** repo in the product interview; the force
  to private was only discoverable in the non-interactive help text.
- **Actual:** Repo created private on a free-tier personal account → GitHub 403 on branch
  protection. Driver fallback fired exactly as designed (BL-002 marker in
  `scripts/host-drivers/github.sh`): loud `[FAIL] Attestation required — cannot proceed`,
  exit status 2, three documented options printed (Pro upgrade / make public / attest), and
  init completed everything else (repo exists, initial push landed 13:26:51Z, 83/83
  verify-install checks pass).
- **Severity:** Confusion (framework behaved correctly and loudly; the User Guide's account
  prerequisite covers it, but nothing warns an org-path user on a free personal account
  *before* init that this exact dead-end is guaranteed; and the interview-vs-forced-private
  collision surfaced only at init).
- **Resolution:** STOPPED for the human decision — this is Karl's call among the three
  documented options. Recorded in APPROVAL_LOG/WALK-STATE once decided.
- **Time lost:** ~10 min (log reading + driver code reading).

---

## SMOOTH NOTES (things that worked as promised)

- **S-1:** Fresh HTTPS clone at `6417a25` — clean, no surprises. All prerequisites already
  present and authenticated (`gh` as kraulerson; semgrep/gitleaks/snyk/claude/docker/jq/node).
- **S-2:** `init.sh --validate-only` — clean JSON echo of resolved config; caught nothing
  wrong because nothing was wrong. Good UX for a scripted run.
- **S-3:** Init end-to-end in 59s with 83/83 verification checks passing, on a language the
  framework had never seen. The TDD commit-gate degraded gracefully for cpp with an explicit
  note: *"no language-specific test-file convention — the gate uses the generic conventions
  (tests/ trees + common test-file names)"* — exactly the right behavior for an unknown language.
- **S-4:** The branch-protection failure path: loud, non-zero exit, precise remediation
  commands, and the driver prints the three real-world options with costs. A failing gate
  that explains itself.

## OBSERVATION-005 — Dual-hatted backup maintainer accepted (control weakening, by declared decision)

- **When/where:** 2026-08-03 ~07:45, organizational pre-condition 5.
- **What:** The framework's backup-maintainer control assumes a second human who validates
  HANDOFF.md independently in Phase 4. Karl designated himself dual-hatted (no second human
  in this walk). Recorded verbatim in APPROVAL_LOG.md row 5 with the weakening acknowledged.
  Not a framework defect — a deployment-reality note for the walk report: solo users on the
  organizational path will hit this same slot with no framework guidance for the
  "organization of one" case.
- **Severity:** Confusion (doc gap at most).
- **Time lost:** none.

## SMOOTH NOTES (continued)

- **S-5:** Recovery loop for ISSUE-004 was exactly as documented: `gh repo edit --visibility public`
  (Karl's decision, driver option 2) → `scripts/check-gate.sh --repair` (skipped already-done
  steps, re-applied protection) → `--preflight` → "Ready: protection verified for org mode."
  First try, no friction. The stepwise-resume design of the repair script is genuinely good.
- **S-6:** All 6 organizational pre-conditions presented to and decided by Karl individually,
  recorded verbatim in APPROVAL_LOG.md (append-only rows) + GitHub issue #1 as the ITSM record.
  The pre-conditions table's append-only design with numbered rows was easy to comply with.

## ISSUE-006 — Org-mode protection + solo GitHub account = unmergeable main; no documented recovery (BLOCKER, workaround pending human decision)

- **When/where:** 2026-08-03 ~07:50, first push after `check-gate.sh --repair` applied org-mode protection.
- **Expected (doc):** Project CLAUDE.md §Branch Protection: *"Until then, the Orchestrator
  creates and merges their own PRs with phase gate review at milestones."* Builder's Guide
  1432: *"commit … push → open PR"* — the framework clearly intends a PR flow the solo
  Orchestrator can complete.
- **Actual:** Org protection bar (applied by the framework's own driver: `enforce_admins:
  true`, `required_approving_review_count: 1`, strict checks) makes that impossible solo:
  1. `git push` → `GH006 … Changes must be made through a pull request` (direct push sealed).
  2. PR #2 opened; `gh pr merge 2 --merge` → *"not mergeable: the base branch policy
     prohibits the merge"* (exit 1). GitHub forbids self-approval of one's own PR; there is
     no second collaborator; `--admin` is both walk-forbidden and neutralized by enforce_admins.
  3. Attestation hatches (`github_free_tier`, `gitlab_free_tier_approvals`) cover
     protection-UNAVAILABLE only — checked `scripts/check-gate.sh` (BL-002 block) and the
     backlog; nothing covers review-UNSATISFIABLE. Governance Framework §178-182
     ("No self-approval") confirms the design assumes ≥2 humans.
- **Severity:** **Blocker** (all merges to main frozen; governance records commit `ecea92c`
  stranded on branch `walk/governance-records`). Practical workaround exists but requires a
  human decision (second reviewer account), hence Blocker-with-workaround, not permanent.
- **Known-ledger check:** no BL/BUG entry found for the solo-account org-mode review dead-end.
  Appears NEW — and is exactly the class of issue a first full-rigor validation should surface:
  the org path was designed for real orgs; the "organization of one" case has no documented story.
- **Resolution:** STOPPED for Karl's decision (options: second controlled account as
  reviewer / real second person / other). To be recorded here + APPROVAL_LOG history once made.
- **Time lost:** ~25 min (diagnosis, doc/ledger search, empirical PR proof).

## ISSUE-006 addendum — resolution decided (2026-08-03)

Karl's decision, verbatim intent: he has the ability to approve merges in GitHub and will
push blocked merges forward himself; every such un-block must be logged in an audit file.
Implemented as the standing protocol in **WALK-UNBLOCK-AUDIT.md** (append-only): agent
prepares PR + green CI, stops, Karl merges in the GitHub UI, entry appended. ISSUE-006
severity stands as logged (the framework still has no documented solo-org story); the
walk is un-blocked by explicit, audited human authority — which is the walk protocol
working as designed.

- **S-7:** The authored cpp CI went green on its first live run (PR #2: `test` 36s, `sast`
  26s) — guarded build steps skipped cleanly pre-scaffold; gitleaks, governance checks, and
  the Semgrep container job all ran. The ISSUE-002 workaround is validated in CI, not just locally.
- **S-8:** Manual-mode intake fill worked exactly as the state machine expects: with every
  cell filled, the blank-cell predicate went to 0 and `scripts/resume.sh` flipped from the
  intake prompt to printing our customized §13 initialization prompt verbatim. Clean handshake
  between a hand-edited intake and the framework's detection.

- **S-9:** ISSUE-006's resolution matured mid-walk: Karl stood up a second GitHub identity
  (`kraulerson-reviewer`) and PR #3 was merged through a genuinely-satisfied required review
  rather than an owner override. The org-mode protection bar is now fully operable solo-with-
  two-hats — worth recording as the practical answer the framework docs currently lack.

## ISSUE-007 — Gate-date auto-record + refused advance = self-inflicted deadlock (MAJOR)

- **When/where:** 2026-08-03 ~09:35, first `process-checklist.sh --start-phase1` attempt.
- **Expected (doc):** Project CLAUDE.md gate sequence: (1) update APPROVAL_LOG, (2) run
  check-phase-gate.sh, (3) run entry command, (4) "Commit APPROVAL_LOG.md and
  .claude/phase-state.json together" — i.e., committing comes AFTER the gate runs.
- **Actual:** Run 1: gate auto-recorded `gates.phase_0_to_1=2026-08-03` from the (uncommitted)
  approval entry, then BLOCKED on "cannot verify commit author — row not yet committed"
  (a WARN arm that increments issues — the documented [WARN] trap). Run 2 (row now committed):
  gate blocked on its own artifact: "[WARN] Phase 0→1 gate has date 2026-08-03 but
  current_phase is still 0" — and the only command that advances current_phase consults the
  gate first. Deadlock: the refused first run wrote state the gate itself rejects forever.
- **Severity:** Major — any transient failure on a gate's first entry attempt (here: following
  the documented commit-last order) permanently wedges the gate.
- **Known-ledger check:** no BL/BUG entry found for auto-record-then-refuse wedging.
- **Resolution:** single invocation `SOIF_PHASE_GATES=warn bash scripts/process-checklist.sh
  --start-phase1` — the documented downgrade knob, printed by the gate itself and documented
  in the User Guide Tier-1 table. Escape-hatch use, logged here per walk rules. Phase advanced
  0→1; gate consistency on that axis restored (date + phase now agree).
- **Time lost:** ~20 min.

## ISSUE-008 — Org self-approval verifier is unsatisfiable for a solo operator; both advertised remedies are dead ends (MAJOR, blocking at every gate)

- **When/where:** 2026-08-03 ~09:40, plain `check-phase-gate.sh` after Phase 1 entry.
- **Expected (doc):** Some satisfiable path for a compliant org project to pass the Phase 0→1
  self-approval verification. The FAIL message offers two: "Have the approver commit the
  APPROVAL_LOG.md entry themselves, or use --force with documented justification."
- **Actual (all verified by execution/code read, check-phase-gate.sh ~lines 870-938):**
  1. `--force` DOES NOT EXIST: `bash scripts/check-phase-gate.sh --force` → "[FAIL] Unknown
     argument: '--force'". The advertised escape is unimplemented.
  2. "Approver commits it themselves" GUARANTEES the failing predicate — the check FAILs
     precisely when commit author == approver.
  3. The check contradicts the User Guide Tier-3 control "Approval log entries authored by
     the approver" outright (org projects can't satisfy both).
  4. Solo dead-end is total: author==approver → FAIL; author≠approver while ambient
     `git config user.name` == approver → WARN "verify the commit author wasn't rewritten"
     which ALSO increments issues (blocks); author unverifiable → blocks. With one human
     identity, every arm blocks. Recurs at EVERY gate incl. the dual 3→4 sign-offs.
  5. The walker reads only the FIRST Approver row per gate section, so an append-only
     superseding entry cannot cure a failing first row.
- **Severity:** Major (Blocker-pattern at every remaining gate without a standing resolution).
- **Known-ledger check:** BL-055 and code-check-gates entries cover blame PRECISION
  (wrong-author shadowing), not the unsatisfiability, the phantom --force, or the
  remedy/predicate contradiction. Appears NEW.
- **Resolution:** STOPPED for Karl — options presented: (A) adopt a recorder/reviewer git
  identity for the repo so approval-row commits are authored by the reviewer persona
  (mechanically satisfiable, two-hat reality documented here); (B) project-wide
  SOIF_PHASE_GATES=warn (documented knob; softens Tier-1 gate to warning everywhere);
  (C) halt and record as terminal for solo-org. Recommendation: A.
- **Time lost:** ~35 min (code read, remedy testing, ledger check).

## ISSUE-008 addendum — resolution decided and implemented (2026-08-03)

Karl selected the **recorder-identity convention** (option A, plain-English "record-keeper
hat"): the repository's git identity is now `kraulerson-reviewer` — the recorder persona that
COMMITS what the named approver (Karl Raulerson) DECIDED. Approval rows keep the approver's
true name; the recorder authors the commits. This satisfies every arm of the self-approval
verifier mechanically and mirrors the framework's own recorder≠approver example (Orchestrator
records Jane Smith's email approval). The unmerged walk/phase0 branch was rebuilt once so the
Phase 0→1 approval row's introducing commit is recorder-authored — rebuild authorized by Karl
as part of this decision and logged in WALK-UNBLOCK-AUDIT.md. The two-hat reality (both
identities are Karl) remains fully documented here and in the audit file.

## OBSERVATION-009 — PR #4 merged early (at Draft state); benign outcome, split verified

- **When/where:** 2026-08-03 15:48:42Z; noticed when `gh pr close 4` reported "already merged".
- **What:** Karl (reviewer account) merged PR #4 while the gate-approval commits were still
  being re-landed locally. Verified by parents of merge b10e572: second parent is 8fc6d4e
  (artifacts only, Manifesto Status: Draft); the Karl-authored approval commits cb5d9dd/82ad56f
  are NOT in main (merge-base checks). Net effect: main carries only the Draft artifacts, and
  PR #5 (walk/phase0-recorder) delivers the complete recorder-authored approval + phase
  advance on exactly that base — no conflict, no self-approval poison on main. The denied
  force-push turned out to be the right protection: the replacement-branch path left every
  published ref append-only.
- **Severity:** Confusion (multi-hat coordination timing; no damage).
- **Time lost:** ~15 min (verification).

## ISSUE-010 — Generated release.yml is an INVALID workflow for a discovered language; every push logs a red run (MAJOR, escalation of ISSUE-003)

- **When/where:** 2026-08-03, noticed in `gh run list` — `release.yml` fails at "startup_failure"
  on every push to any branch.
- **Expected:** README: "Adding a new language requires one file: a CI template." Release
  pipelines carry by-design signing TODOs but should be VALID YAML that simply no-ops until configured.
- **Actual:** `get_release_vars()` `*)` arm substitutes literal comment text into a
  structural field: `- uses: # TODO: Add setup action for your language`. `uses:` with a
  comment value is not a valid step → GitHub rejects the whole workflow at parse time →
  a red "startup_failure" run is logged on EVERY push (30834081926 etc.). It never ran a
  release; it can't even parse.
- **Severity:** Major (escalates ISSUE-003 from "TODO null-steps" to "invalid workflow
  polluting the Actions history with red runs from day 1"). On a real project this is a
  standing false alarm in the CI dashboard.
- **Known-ledger check:** no BL/BUG entry for cpp/discovered-language release.yml invalidity.
- **Resolution:** DEFERRED to Phase 4 per framework sequencing (release pipeline is a Phase 4
  artifact) — will author valid C++ macOS build/sign steps then. Logged now because the red
  runs are visible immediately and would confuse any observer. Not worked around mid-Phase-1
  (the CI pipeline `ci.yml`, which IS the enforcement floor, is valid and green).
- **Time lost:** ~5 min.

## ISSUE-011 — Phase 2 --verify-init lockfile check has no C++/CMake entry; project_scaffolded un-auto-verifiable (MINOR)

- **When/where:** 2026-08-03, `process-checklist.sh --verify-init` after building the scaffold.
- **Expected (doc):** User Guide: --verify-init "auto-detects completed steps by inspecting
  your environment (git remote, CI pipeline, scaffold, hooks)." The scaffold genuinely exists
  (a Qt6 app that configures, builds, launches headless, and passes ctest — proven locally).
- **Actual:** `[FAIL] project_scaffolded — no lockfile found (package-lock.json yarn.lock
  pnpm-lock.yaml Pipfile.lock poetry.lock Cargo.lock go.sum pubspec.lock Package.resolved
  gradle.lockfile packages.lock.json)`. None of these are C++. A CMake project's dependency
  pins live in CMakeLists.txt itself (FetchContent GIT_TAG = exact tag/commit pins) — the
  desktop Platform Module even documents gradle.lockfile for JVM but nothing for CMake/C++,
  the one language the framework advertises as the drop-in extension example.
- **Severity:** Minor (a real, buildable scaffold exists; only the auto-detection heuristic
  doesn't fit C++). Consistent with the ISSUE-002 family: C++ is a first-class advertised
  extension but several tooling touchpoints assume a lockfile-bearing ecosystem.
- **Known-ledger check:** no BL/BUG entry for a C++ lockfile in verify-init.
- **Resolution:** used the documented individual-step path (`--complete-step
  phase2_init:project_scaffolded`) — legitimate per the User Guide ("When the agent completes
  Phase 2 initialization steps individually… the system auto-sets phase2_init.verified = true
  once all steps are marked complete"). The dependency pins ARE present and exact in
  CMakeLists.txt (doctest v2.4.11 by tag; Qt via find_package with a 6.2 floor; further deps
  pinned as features add them). NOT a bypass — the real artifact exists and was verified.
- **Time lost:** ~5 min.

- **S-10:** Recorder-identity convention (ISSUE-008 fix) paid off at the Phase 1→2 gate: the self-approval verifier passed cleanly (commit author kraulerson-reviewer != approver Karl Raulerson), gate exit 0, phase_1_to_2 auto-recorded, snapshot created — no warn-knob, no friction. The fix generalizes to every remaining gate.
- **S-11:** The authored C++ scaffold is real, not a stub: Qt 6.11.1 app configures under CMake, builds 14 targets clean, launches headless (offscreen QPA) running its event loop, terminates cleanly, and ctest is 1/1 green. clang-format clean. The from-scratch-renderer project has a working foundation on day 1 of Phase 2.

- **S-12:** Strict-mode enforcement audit demonstrated end-to-end (the good kind of finding):
  my first scaffold commit used `feat:`, the Build-Loop commit-msg gate HARD-BLOCKED it (no
  active Build Loop), I ABANDONED it and re-committed as `chore:` — and
  `.claude/bypass-audit.json` recorded the event as
  `type: terminal_commit_blocked, gate: commitmsg_buildloop, final_outcome: abandoned`. The
  block was correct, compliance was the response (no --no-verify, no bypass), and the audit
  trail captured it faithfully. This is exactly the Tier-2 "route around the block? no — the
  audit is the point" behavior the docs promise, observed live.

- **S-13:** The mandated per-feature security audit EARNED ITS KEEP on the very first real
  feature. Five parallel specialist agents against the untrusted-.pptx parser found 3 Critical
  + 1 High that would otherwise have shipped: a ZIP64 integer-wrap that bypassed the zip-bomb
  caps into a heap overflow, a recursive stack-overflow crash on nested XML, a silent
  slide-drop that would have sent "go to slide N" to the WRONG slide in a live exec talk, and
  a shape-flood OOM. All fixed test-first with regression tests bound to finding IDs. This is
  the framework's core value proposition (TDD + per-feature audit catching AI blind spots)
  working exactly as advertised — the silent-slide-drop in particular is the kind of
  plausible-looking bug that passes casual review and fails catastrophically live.
- **S-14:** The security_audit Build-Loop step is artifact-gated: it REFUSED to mark complete
  until docs/security-audits/f1a-deck-loader-security-audit.md existed with a machine-readable
  "0 Open findings" summary, and it correctly refuses the force-override to non-interactive
  agents ("HUMAN ONLY… do NOT retry… ESCALATE"). I produced the real artifact rather than
  escalating. An audit the gate can't read is treated as a failing audit — good design.

## ISSUE-012 — commit-msg TDD gate is a hard block on Production tier but did NOT fire on a large feat: with tests (SMOOTH-adjacent note, not a defect)

- **When/where:** 2026-08-03, F1a feat: commit (caa34ab).
- **Note:** The BL-072 TDD-ordering gate (hard block on Production) passed the F1a commit
  because the commit shipped implementation WITH its tests (test_deck_loader.cpp staged
  alongside deck_loader.cpp) — exactly the co-location the gate checks for. Recording as a
  positive: the gate's heuristic (test file present with impl) matched genuine TDD here. NOT
  a finding — the gate behaved correctly; logged so the walk report can note the TDD gate
  passing a real test-first feature (vs. the earlier chore: scaffold which correctly needed
  no test).
