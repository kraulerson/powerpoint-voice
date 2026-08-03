# UAT Session 1 — Tester Results

**Testers:** 3 parallel agent-testers (sanitizer / real-PowerPoint-compatibility / exploratory
rendering) + human orchestrator (Karl, deferred real-deck check to rehearsal — proceeding on the
remediation recommendation).
**Date:** 2026-08-03

## Automated results

| Scenario | Result |
|---|---|
| Full unit/integration suite (`scripts/run-tests.sh`) | PASS — 40/40 (pre-remediation), 48/48 (post) |
| ASan + UBSan over the suite incl. hostile fixtures | PASS — clean, no memory/UB errors |
| Semgrep (p/owasp-top-ten, p/security-audit) | PASS — 0 findings |

## Findings (exploratory + real-PowerPoint compatibility)

The synthetic fixtures passed, but the agent-testers found the renderer did not handle
real-PowerPoint OOXML features. 7 bugs filed (BUGS.md BUG-1..7):

- **BUG-1 (SEV-1):** theme/inherited text color → invisible text on dark decks.
- **BUG-2 (SEV-1):** placeholder position inheritance from layout not resolved → overlap/clip.
- **BUG-3 (SEV-2):** grouped shapes hidden behind a placeholder box.
- **BUG-4 (SEV-2):** long text not wrapped → truncated.
- **BUG-5 (SEV-2):** multi-run color/format lost.
- **BUG-6 (SEV-2):** line breaks dropped.
- **BUG-7 (SEV-3):** bullets/indent ignored.

CORRECT (no bug): multi-paragraph stacking, element positioning, z-order, letterboxing.

## Disposition

All 7 triaged **Fix Now** by the Orchestrator (Karl, 2026-08-03) and **remediated test-first
in this session** — 8 new regression tests, all green; verified visually via `render_preview`
(themed dark deck now shows readable red accent + white plain text; multi-run RGB shows three
colors). See BUGS.md for status (all Fixed) and the documented remaining limitations.
