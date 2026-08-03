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
