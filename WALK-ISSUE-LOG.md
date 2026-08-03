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
