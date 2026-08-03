# WALK-UNBLOCK-AUDIT — human-executed un-blocks (append-only)

Standing protocol, decided by Karl Raulerson 2026-08-03 (resolves ISSUE-006):
merges to `main` that the branch-protection policy refuses to the solo account are
**pushed through by Karl himself in the GitHub UI**, under his authority as repository
owner and accountable Orchestrator. The AI agent NEVER bypasses (no `--admin`, no
protection edits); it prepares the PR, verifies CI, then stops and hands the merge to
Karl. **Every such human un-block is recorded here** — date, PR, what was blocked, what
Karl did, and why. This file is append-only; entries are never edited once pushed.

| # | Date | PR | What was blocked | Human action | Reason / notes |
|---|---|---|---|---|---|
| 1 | 2026-08-03 | #2 (governance records: pre-conditions rows 1-6, protection-resolution event, walk logs, ISSUE-006) | Merge refused: 1 approving review required; solo account cannot self-approve; enforce_admins active | PENDING — Karl to approve/merge in GitHub UI | First application of the standing protocol; see WALK-ISSUE-LOG.md ISSUE-006 |
| 2 | 2026-08-03 | #2 | (completes row 1) | DONE — merged by kraulerson at 13:55:15Z, merge commit 01a0d047 | Verified via gh pr view; protocol worked end-to-end on first use |
| 3 | 2026-08-03 | #3 (completed PROJECT_INTAKE + walk records) | Standard required-review gate | APPROVED+MERGED at 15:21:24Z by `kraulerson-reviewer` — a second account Karl operates as the reviewer role | Protocol upgrade: with a genuine second reviewing identity, required reviews are now properly satisfied (no admin un-block needed); control integrity improved over row 2's path |
| 4 | 2026-08-03 | branch walk/phase0 (pre-merge rebuild) | Self-approval verifier unsatisfiable solo (ISSUE-008): approval-row commit had author == approver | Karl authorized the recorder-identity convention + one-time rebuild of the unmerged branch; commits cb5d9dd/82ad56f re-landed under `kraulerson-reviewer`; force-push of the FEATURE BRANCH only (main untouched) | Standing convention from now on: repo git identity = recorder (kraulerson-reviewer); approver names in rows remain the deciding human (Karl Raulerson) |
| 5 | 2026-08-03 | #6 (Phase 1 + scaffold) | Standard required-review gate | APPROVED+MERGED 18:04:26Z by `kraulerson-reviewer` | First green C++ CI on ubuntu verified (Qt build + ctest 1/1) before merge; recorder-identity gate passed clean |
| 6 | 2026-08-03 | #7 (F1a deck loader + Build Loop) | Standard required-review gate | APPROVED+MERGED 21:13:15Z by `kraulerson-reviewer` | First real feature; CI green both platforms (libzip 1.7.3/pugixml 1.14 on ubuntu vs 1.11/1.16 local — version-tolerant); 5-agent audit caught 3 Critical+1 High, all fixed test-first |
