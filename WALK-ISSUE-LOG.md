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

- **S-15:** First real cross-platform CI divergence and it was a genuine project issue (not a framework finding): ubuntu splits libzip CLI tools out of libzip-dev, and libzip's CMake targets file asserts /usr/bin/zipcmp exists, so find_package(libzip) errored on ubuntu though it passed on macOS (brew bundles the tools). Fixed by adding libzip-tools to .github/ci-deps-apt.txt. The ci-deps-apt.txt mechanism I built into the cpp.yml template made this a one-line fix — the extension pattern held up under a real cross-distro packaging quirk.

- **S-15 follow-up:** The libzip fix evolved: libzip-tools does not exist as an Ubuntu package, so the robust fix was to locate libzip AND pugixml via **pkg-config** (IMPORTED_TARGET) instead of each library's CMake config — pkg-config finds them cleanly on both brew and apt without the tools-target assertion. Required installing pkgconf locally (standard dev tool) and adding pkg-config to CI apt deps. Local build recipe now needs PKG_CONFIG_PATH for the brew kegs.

- **S-16:** F1b renderer built through the full Build Loop. The per-feature security audit
  ONCE AGAIN caught serious real bugs on the second feature: a font-size giant-glyph hang
  (a hostile deck declaring an absurd font freezes the app mid-talk), an untrusted-image-codec
  gap (QImage::fromData would invoke CVE-prone TIFF/WebP/GIF decoders on attacker bytes —
  now allow-listed to PNG/JPEG), and unbounded text volume. Verified VISUALLY too: a GIF
  behind a .png name renders a "missing image" placeholder (allow-list working), a real PNG
  decodes and displays. Two features, two audits, ~7 Critical/High bugs caught and fixed
  test-first that plain "write code + run tests" would have shipped. The framework's core
  claim — per-feature adversarial audit catches AI blind spots — is now demonstrated twice.
- **S-17:** Rendering fidelity confirmed with real pixels: bold white title on the dark
  background, correctly sized/positioned; unsupported table → labeled placeholder box;
  images decode and scale. The from-scratch-renderer bet (intake §11 risk 1) has a working,
  security-hardened text+images foundation. Full suite 40/40 on macOS; CI to confirm ubuntu.

- **S-18:** The framework's BL-125 project-test gate surfaced a real setup gap on a source
  commit — "no test command configured, PROJECT TESTS NOT ENFORCED" — nudging me to wire up
  local commit-time test enforcement. Closed it: authored scripts/run-tests.sh (env + cmake
  build + ctest, 40 tests in ~3s incremental) and pointed .claude/test-command at it. Now the
  commit gate actually runs the suite, not just the presence-of-a-test-file heuristic. A
  helpful, non-blocking nudge that improved the project's own rigor.
- **S-19:** CI-caught cross-platform test fragility (a positive): the placeholder-slide test
  sampled the exact slide center, which hit the 'Slide unavailable' glyph under Ubuntu's
  DejaVu font but fell on background under macOS's — macOS passed by luck, ubuntu CI failed
  loudly. Fixed the test to sample a corner of the dark fill. Exactly the value of building
  cross-platform in CI from day one: a font-dependent assertion that would have been a
  mystery later got caught immediately.

- **S-20:** The FIRST UAT session was the single highest-value quality event of the walk so
  far. The unit tests + two security audits all passed, but UAT's exploratory agent-testers
  (sanitizer / real-PowerPoint-compat / rendering) found that the renderer — green on
  synthetic fixtures — would render Karl's REAL exec deck with INVISIBLE TEXT (theme/inherited
  colors defaulting to black on dark slides), misplaced placeholders, hidden grouped text,
  truncated bullets, and mono-color runs. Seven real bugs (2 SEV-1) that every prior gate
  missed because the fixtures were too minimal. This is the exact gap UAT exists to close
  ("passes synthetic tests" ≠ "works on real input"), and the framework's every-2-features
  UAT mandate surfaced it before a single feature more was built. All 7 fixed test-first
  (48 tests), sanitizer-clean, and verified in actual pixels. Strongest possible evidence
  for the framework's UAT discipline.
- **S-21:** UAT process enforcement worked as designed and productively: the commit gate
  HARD-BLOCKED my mid-session commit ("UAT session in progress — complete all steps") and the
  results_received step refused to advance without a real submission artifact (offering a
  logged solo-attest escape I didn't need — the agent-testers' results ARE the artifact). The
  9-step UAT checklist forced the full find→triage→remediate→gate cycle before any code landed.
  Also: the BL-125 test-command I wired earlier now runs all 48 tests at every source commit —
  local commit-time test enforcement is live.

## OBSERVATION-013 — BL-120 security-audit gate rejects a QUALIFIED "Yes" verdict (MINOR)

- **When/where:** 2026-08-04, F2/F3 `build_loop:security_audit` step.
- **What:** The gate parses the audit doc's `**All findings resolved:**` line and requires an
  UNQUALIFIED `Yes`. I wrote `Yes (no open items; deferred items are Low and logged)` — a
  natural human phrasing — and the step was blocked with a clear message ("not an unqualified
  Yes"). Fixed by moving the parenthetical to its own line so the verdict is a bare `Yes`.
- **Assessment:** Defensible by design (a machine-checkable verdict must be unambiguous) and
  the error message was clear and actionable, so NOT a defect — but a human writing "Yes, with
  notes" will hit it. Logged as a minor usability rough edge for the walk report; a one-line
  relaxation (accept `Yes` as the first token) would remove the surprise.

- **S-22:** THIRD feature, THIRD per-feature audit catching a real ship-blocker. The F2/F3
  adversarial audit (2 agents) found a High: the dispatch sink runs synchronously inside
  `onPhrase`, which the real recognizer will call from an AUDIO THREAD via a C callback — a
  sink exception would cross that boundary as UB / `std::terminate`, i.e. a crash mid-talk. Plus
  a Medium availability bug (dictation punctuation like "Next slide." silently no-op'd every
  command) and a Medium reentrancy hole. All fixed test-first (3 red→green regressions), ASan+
  UBSan clean, Semgrep 0. Equally important, the audit CONFIRMED the two safety-critical
  properties clean under attack: audience speech cannot false-trigger a command (phrase-level
  exact match), and no heard text is ever logged (Bible §8). The "audit every feature" claim
  keeps paying out — including on a small, pure-logic feature where it would be tempting to skip.

- **S-23:** The framework's `pending-approval.sh` sentinel + `escalate` pattern worked cleanly
  as the STOP mechanism for two structured decisions this session (the F2/F3 test-gate assertion
  approval, and the scope-split decision). Writing the sentinel before asking Karl, then
  `--resolve --decision accept` after his pick, kept the pre-commit gate and stop-hook aware
  that a human was deciding — no premature commit/stop drift. This is the documented "structured
  decision points" flow behaving exactly as CLAUDE.md prescribes.

- **S-24 (self-inflicted, honest stumble):** My first compile of the controller failed with 5
  cryptic parse errors — I had named a local `bool emit`, and Qt reserves `emit` as a macro, so
  it expanded to nothing. Not a framework issue; a normal Qt gotcha. Worth logging only as
  evidence that the fast local build (~seconds, incremental) caught it instantly with a clear
  compiler pointer, and the fix (rename to `shouldDispatch`) was trivial — the tight build/test
  loop the toolchain setup gives makes these self-inflicted errors cheap.

- **S-25 (self-inflicted, honest stumble + gap closed):** PR #13's Ubuntu CI failed on the
  clang-format check — I formatted the SOURCE files after the UAT-2 remediation but not the two
  TEST files I'd edited, and the local commit gate (`scripts/run-tests.sh`) runs build+ctest but
  NOT clang-format, so the violation passed locally and only surfaced in CI (one wasted round-trip).
  Root cause is a local/CI gate asymmetry, not the framework. Closed the gap: added the same
  `git ls-files … | clang-format --dry-run --Werror` check to run-tests.sh, so a format-only
  violation now fails at commit time. Lesson: mirror every CI gate in the local commit gate.

- **Scope decision (2026-08-04):** F2/F3 was split with Karl's approval — this feature delivers
  the voice-command GRAMMAR + DISPATCH (pure, fully unit-tested, audited); the Vosk speech
  engine + microphone capture is carved into a follow-on feature (UAT-validated, needs the
  bundled-model dependency decision). Rationale: the engine can only be validated with real
  audio, so it does not fit unit-test-first — separating it keeps the tested logic honest.

- **OBSERVATION-014 — ADR-pinned Vosk version (0.3.45) has NO macOS build (real dep-availability
  surprise):** setting up the voice-engine deps, the exact version ADR-0001 named — Vosk 0.3.45 —
  turned out to ship no macOS binary at all (Linux + Windows only, on both the GitHub release and
  PyPI). The last version with a macOS `universal2` (arm64) build is 0.3.44 (PyPI-only; there is
  not even a `v0.3.44` git tag). Verified against the live registries and pinned 0.3.44; took the
  stable header from the v0.3.45 tag and ABI-verified every used symbol resolves in the 0.3.44 lib
  (`nm -gU`). Lesson for the framework's ADR step: an architecture ADR should pin a dependency
  version only after confirming a build exists for every TARGET platform — the Bible/ADR asserted
  0.3.45 without that check, and it was wrong for the primary (macOS) target. Not blocking (0.3.44
  works), but a real gap between "chose the library" and "the library ships for our platforms."
  Dep decision: Karl chose full self-containment — libvosk + the 40MB model committed via git-LFS,
  everything pinned by SHA-256, wheels verified against PyPI digests (`third_party/PROVENANCE.md`).

- **S-27 — the single highest-value event of the walk so far: a pre-implementation DESIGN review
  caught a strategy error the whole process had missed.** Before writing any voice-engine code, an
  8-agent design workflow (4 parallel deep-dives -> synthesis -> 3 adversarial critics) reviewed the
  design. All three critics returned NEEDS_CHANGES, and one observation generalized into a
  project-level finding: every voice failure path terminated in "use the clicker", but keyboard
  control (F6) did not exist — and checking that revealed the app had **no presentation UI at all**
  (main.cpp opens a dark StartView; the loader/renderer/command logic were a library wired to
  nothing) with 6 days to the live talk. Four features had been built to production rigor without a
  single one being *usable*. Karl resequenced: F7 presentation UI + F6 keyboard FIRST, voice after
  (BUG-18). **Framework gap this exposes:** the Build Loop enforces per-feature rigor
  (tests/audit/docs) but nothing in it ever asks "is the product demonstrable end-to-end yet?" The
  MVP Cutline lists features but implies no ordering by user-visible value, and the UAT cadence
  tests *features*, not the *product*. A "walking-skeleton first" or "is it demonstrable?" checkpoint
  between features would have caught this at F1b. Recommend the framework add one.

- **S-28 — the same design review found a SEV-2 security regression in already-merged, already-
  UAT'd code (BUG-17), traceable to a fix the walk itself approved.** The UAT-2 BUG-11 remediation
  (which I recommended and Karl approved) added bare "resume"/"continue"/"pause" to fix
  stuck-in-Paused. The critics showed that this hands the audience a ONE-WORD un-pause during Q&A —
  defeating the primary documented mitigation for TM-002/019 in exactly the window it protects —
  made worse by the filler strip reducing "okay lets continue" / "and now continue" to a lone word.
  I verified it directly with command_probe before reporting. Fixed test-first (object now required;
  7 assertions). Honest lesson: a UAT remediation is a code change like any other and deserves the
  same adversarial review as the original feature — the walk's UAT loop had no re-audit step after
  remediation, so the regression passed the gate, CI, and a merge.

- **OBSERVATION-015 — no non-interactive way to ABANDON a started Build Loop (MINOR).** After Karl
  resequenced, the open `F2-F3-voice-engine` loop (0/6, no code written) had to be abandoned.
  `--reset build_loop` is interactive-only (Y/N + a terminal) and refuses agent sessions by design.
  `--start-feature` for the new feature DID succeed, printing `[WARN] Previous feature
  'F2-F3-voice-engine' was not recorded` — a good, honest warning that leaves an accurate trail
  (the loop was abandoned, not completed). I deliberately did NOT run `--record-feature` for it,
  since no work was done and recording would have inflated the UAT counter with a phantom feature.
  Working as designed, but a documented `--abandon-feature` (recording the abandonment + reason)
  would fit the framework's audit philosophy better than a warning that a later reader must
  interpret.

- **S-26 (self-inflicted, honest stumble + gap):** the first attempt to commit the vendored deps
  HUNG (killed at 2min). Diagnosed empirically: gitleaks (3s) and semgrep (4s) on the staged set
  were fine — the stall was the clang-format check I'd just added to run-tests.sh (S-25) formatting
  the 4MB vendored `miniaudio.h`. Vendored third-party code must be excluded from our linters:
  added `':(exclude)third_party/**'` to the clang-format checks in both run-tests.sh and CI (and
  clang-tidy in CI). A direct consequence of S-25's new gate meeting a 4MB vendored header — the
  fix I added needed a scope carve-out for vendored code, which is standard practice I should have
  applied up front.

- **OBSERVATION-016 — clang-tidy has NEVER run: a gate that silently no-ops, while the Bible claims
  it is enforced (MODERATE).** Found by the F7 design review. `.github/workflows/ci.yml` guards the
  lint step with `if: hashFiles('.clang-tidy') != ''` — and **no `.clang-tidy` file has ever
  existed**, so the step has been skipped on every CI run of the walk while reporting the job green.
  Meanwhile `PROJECT_BIBLE.md` section 10 states style is "enforced by `.clang-format` ... and
  `.clang-tidy`". So an approved control was documented as active, appeared green in CI, and was
  doing nothing — the exact shape of a compliance illusion. Measured the cost of turning it on:
  108 warnings on existing `src/`, but 82 of them are two stylistic checks
  (`misc-const-correctness` 46, `misc-include-cleaner` 36); with a focused bug-finding check set only
  ~12 are substantive (narrowing conversions 8, internal linkage 2, no-recursion 1, and one
  `bugprone-empty-catch` which is the deliberate audited exception backstop in
  recognizer_controller.cpp and needs a NOLINT with a reason). Cheap to fix, so it will be enabled.
  **Framework finding:** a `hashFiles()`-guarded CI step that silently skips when its config is
  absent is a footgun — the generated pipeline should either ship the config it guards on, or FAIL
  loudly when a documented control is unconfigured. A skipped control must never look like a passed
  control. Recorded for the framework's CI-template design.

---

# FINDINGS INDEX & CLASSIFICATION (added 2026-08-04)

**Why this exists.** Karl asked, mid-walk, whether framework-correctable findings were being
tracked distinctly. Auditing this log to answer honestly turned up three record-keeping defects of
my own, recorded here rather than hidden:

1. **Two of the walk's most significant FRAMEWORK findings were filed as "smooth notes"** (S-27,
   S-28) — under a section header that reads "things that worked as promised". They are promoted
   below to **ISSUE-017** and **ISSUE-018**. The original S-27/S-28 text stays where it is (this
   log is append-only); the promotion is recorded here.
2. **OBSERVATION-014/015/016 were written as bullets, not `##` headings** like OBSERVATION-005/009/
   013, so any structural scan of the log misses them. Their IDs and content stand; noted so the
   final report reads the whole file, not just the headings.
3. **There was no FRAMEWORK vs PROJECT split.** Framework findings (things Solo Orchestrator itself
   could fix), project findings (our own bugs and self-inflicted stumbles), and smooth notes were
   intermixed. This index separates them.

Classification is the point of the walk's second purpose: **only FRAMEWORK rows below are candidate
fixes for solo-orchestrator.** PROJECT rows are our own and are not framework defects.

## A. FRAMEWORK findings — candidate fixes for Solo Orchestrator

| ID | Sev | Finding | Status |
|---|---|---|---|
| ISSUE-001 | Minor | README language-extension path omits the host directory | Reported |
| ISSUE-002 | Major | Language auto-discovery accepts a new language, but `generate_ci` silently ships the `other.yml` skeleton — a discovered language gets a non-functional pipeline | Worked around (authored `cpp.yml`) |
| ISSUE-003 | Major | `get_release_vars`: unknown language yields null `uses:` TODO steps in release.yml | Open (deferred to Phase 4) |
| ISSUE-004 | Major | Org mode forces a private repo; free-tier GitHub cannot protect private repos → init exits "Setup INCOMPLETE" | Resolved by decision (public repo) |
| ISSUE-006 | **Blocker** | Org-mode branch protection + a solo GitHub account = unmergeable main, with no documented recovery | Resolved (human-merge protocol + audit file) |
| ISSUE-007 | Major | Gate-date auto-record + refused advance = self-inflicted deadlock | Worked around (documented knob, single use) |
| ISSUE-008 | **Blocker** | Org self-approval verifier is unsatisfiable for a solo operator; both advertised remedies are dead ends | Resolved (recorder-identity convention) |
| ISSUE-010 | Major | Generated `release.yml` is an INVALID workflow for a discovered language; every push logs a red run | Open (deferred to Phase 4) |
| ISSUE-011 | Minor | Phase 2 `--verify-init` lockfile check has no C++/CMake entry | Reported |
| OBS-013 | Minor | BL-120 security-audit gate rejects a QUALIFIED "Yes" verdict ("Yes (no open items…)") | Worked around |
| OBS-014 | Moderate | The ADR/architecture step let a dependency version be pinned without verifying a build exists for every TARGET platform — ADR-0001 pinned Vosk 0.3.45, which ships no macOS build at all | Corrected (0.3.44) |
| OBS-015 | Minor | No non-interactive way to ABANDON a started Build Loop; `--reset` is interactive-only. A documented `--abandon-feature` (recording the reason) would fit the audit philosophy better than a warning | Reported |
| OBS-016 | Moderate | **A gate that silently no-ops:** ci.yml guards clang-tidy on `hashFiles('.clang-tidy')`, no such file was ever shipped, so the step skipped on every run while the job reported green — and the Bible documented the control as enforced. A skipped control must never look like a passed control | Being fixed |
| **ISSUE-017** | **Major** | **No demonstrability checkpoint.** The Build Loop enforces per-feature rigor (tests → audit → docs) and UAT tests *features*, but nothing ever asks "is the PRODUCT runnable end-to-end yet?" Four features shipped to production rigor while the app was still a dark window that could not open a deck — discovered only 6 days before the live talk, by an ad-hoc design review. A "walking skeleton first" / "is it demonstrable?" checkpoint between features would have caught it at F1b. *(promoted from S-27)* | Reported |
| **ISSUE-018** | **Major** | **UAT remediation gets no re-audit.** The 9-step UAT checklist ends at `gate_passed`; fixes written during remediation never face the adversarial security audit the original feature did. The BUG-11 fix (approved in UAT-2) introduced a SEV-2 regression — a one-word audience un-pause — that then passed the gate, CI and a merge, and was caught only by a later unrelated design review. A remediation is a code change and deserves the same audit. *(promoted from S-28)* | Reported |

**Framework positives worth reporting too:** the per-feature security audit caught real
ship-blocking bugs on **every** feature (F1a 3 Critical + 1 High; F1b 1 Critical + 2 High; F4
overflow/wrong-jump; F2/F3 an audio-thread `std::terminate`); UAT-1 caught 7 real-deck bugs incl. 2
SEV-1 that all prior gates missed; the pending-approval sentinel and the commit/test gates behaved
exactly as documented. These belong in the report alongside the defects.

## B. PROJECT findings — ours, not the framework's

| ID | Finding |
|---|---|
| OBS-005 | Dual-hatted backup maintainer accepted (control weakening, by declared decision) |
| OBS-009 | PR #4 merged while still in Draft; benign outcome, split verified |
| S-24 | Self-inflicted: named a local `bool emit` — Qt reserves `emit` as a macro (5 cryptic parse errors) |
| S-25 | Self-inflicted: formatted sources but not test files; the local gate lacked CI's clang-format check → one wasted CI round-trip. Gap closed |
| S-26 | Self-inflicted: the clang-format check added in S-25 then stalled a commit on the 4MB vendored `miniaudio.h`; vendored code now excluded from our linters |
| ISSUE-012 | (positive) The TDD commit gate correctly passed a real test-first feature |

## C. Smooth notes
S-1 … S-23 (minus those reclassified above): things that worked as promised. Retained for the
report's balance section — a dogfood report that only lists defects is not an honest one.

## Tally (supersedes earlier counts)
**18 numbered findings** — 15 FRAMEWORK (2 Blocker, 7 Major, 2 Moderate, 4 Minor) + 3 PROJECT/
governance — plus 3 self-inflicted project stumbles (S-24/25/26) and ~23 smooth notes.

---

## ISSUE-019 — The Build Loop cannot express a STAGED feature: no `feat:` commit is possible until the whole feature is done (MAJOR)

- **When/where:** 2026-08-04, F7 (`F7-presentation-ui`), first implementation commit.
- **What happened.** Karl approved the FULL F7 scope, which the design review sized at **10 stages
  and 214 assertions**. I therefore staged it (Karl-approved: "sequenced inside F7"), gated Stage 1
  with him, and built the first increment — `PresentationController`, the single slide-index funnel
  (BUG-16 + the quit-confirm matrix), 28 new tests, 121 green. The `feat:` commit was then **blocked**:

      [FAIL] pre-commit gate: 'feat(F7-presentation-ui)' commit blocked — Build Loop incomplete.
      Missing step: implemented

- **Why this is a real limitation, not a misuse.** `build_loop:implemented` attests that the FEATURE
  is implemented. F7 is 1 of 10 stages done, so marking it would be a false attestation — precisely
  the "synthetic Build Loop step completion" that project CLAUDE.md classifies as
  `refuse_to_recommend`. The only compliant paths are therefore:
  (a) accumulate all 10 stages in the working tree and land ONE enormous `feat:` commit — which
      destroys reviewability, bisectability and incremental CI, and is the exact anti-pattern the
      rest of the framework works to prevent; or
  (b) split the feature into many smaller features, one Build Loop each.
- **Assessment.** The 6-step Build Loop implicitly assumes **one feature = one atomic implement step
  = one commit-ready unit**. That holds for a small feature (F1a, F4, F2/F3 all fit). It does not
  hold for any feature large enough to need staging — and the framework provides no vocabulary for
  a staged feature: no sub-steps, no "increment complete", no way to say "implemented through stage
  N". The gate then pushes the operator toward either a false attestation or a mega-commit.
- **Suggested framework fix:** either (1) allow repeatable `--complete-increment "stage name"` inside
  `implemented`, authorizing `feat:` commits while keeping the step open until an explicit
  `--complete-step implemented`; or (2) document, in the Builder's Guide, that a feature too large
  for one implement step MUST be decomposed into multiple features before `--start-feature`, and have
  `--start-feature` warn when a feature's approved test-gate exceeds some assertion count. Silence
  here leaves the operator to discover the constraint only after the work is written.
- **Not bypassed.** No `--no-verify`, no synthetic step completion. Escalated to Karl for the
  structural decision (split into sub-features vs. one accumulated commit).

---

## ISSUE-020 — The enforced UAT checklist omits the archive step the same document mandates (MODERATE)

- **When/where:** 2026-08-04, found because **Karl asked** "are all UAT test results being logged
  like they are supposed to? I don't see many in the repo." He was right; auditing to answer
  honestly produced this finding.
- **What.** Project `CLAUDE.md` (UAT Test Sessions) states: *"After completion and review, archive
  to `docs/test-results/[date]_uat-session-N-vX.html`."* The **enforced** `uat_session` checklist is
  nine steps — `agents_dispatched, template_generated, orchestrator_notified, results_received,
  completeness_verified, bugs_consolidated, triage_complete, remediation_complete, gate_passed` —
  and **none of them is the archive**. Verified: `docs/test-results` appears in `scripts/` only in
  `check-maintenance.sh` and `check-phase-gate.sh`, and only for dependency/security SCAN artefacts,
  never for UAT sessions.
- **Consequence, measured on this walk.** Two UAT sessions were run to completion, both passed
  `gate_passed`, both merged with green CI — and **neither was archived**. `docs/test-results/` held
  a single unrelated file. An operator who follows the gated process exactly, and whose work is
  therefore certified complete by the framework, silently omits an artefact the same governing
  document requires. Nothing anywhere reports the omission.
- **Two further structural drifts found in the same audit** (mine, not the framework's): UAT-1 had no
  `agent-results/` directory — its three testers' results were flattened into one file under
  `submissions/` — and neither session created the `templates/` subdirectory the spec names. The
  session layout is documented in prose and never validated, so drift accumulates silently.
- **Assessment.** This is the same failure shape as ISSUE-016 (a CI control guarded on a file that
  never existed, reporting green while doing nothing) and ISSUE-017 (rigorous per-feature gates that
  never ask whether the product works): **the enforced control and the documented procedure
  disagree, and the enforcement is what people follow.** A checklist that certifies completion while
  a mandated artefact is missing is worse than no checklist, because it produces false confidence.
- **Suggested framework fix:** add `results_archived` as a tenth `uat_session` step with an artifact
  check (the same BL-120-style check already used for the security-audit doc), and have
  `--start-uat N` scaffold `templates/`, `agent-results/` and `submissions/` so the layout cannot
  drift. Both are small and would have caught this automatically.
- **Remediated here:** both sessions archived to `docs/test-results/` with a README explaining the
  naming, the Markdown-vs-HTML fallback, and why the archive was late; UAT-1's structure normalised.
- **My share of it, stated plainly:** the instruction is in the project CLAUDE.md, I had read that
  file, and I did not do it — I followed the enforced gates and treated their completion as
  completeness. That is exactly the trap the finding describes, and I walked into it.
