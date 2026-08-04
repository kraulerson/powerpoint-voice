# UAT Session 2 — Received Submissions

**Date received:** 2026-08-04
**Features:** F4 (number parser), F2/F3 (voice-command matcher + controller) — pure command logic.

## Tester submissions received (substantive results)

Three parallel agent-testers were dispatched and all three submitted results (raw outputs in
`../agent-results/`):

1. **Automated suite** (`../agent-results/01-automated-suite.md`) — ALL GREEN: 86/86 (test time),
   4-run deterministic, ASan+UBSan clean (354 assertions), probe sanity all correct.
2. **Malicious/chaotic user** (`../agent-results/02-malicious-user.md`) — could NOT force a false
   command or crash (safety property confirmed); found the "grammar too strict" missed-command
   findings (BUG-11 SEV-1, BUG-12 SEV-2, plus SEV-3s).
3. **Cross-platform / integration** (`../agent-results/03-cross-platform.md`) — Ubuntu CI green,
   normalization locale-independent (verified under tr_TR), no renderer/F4 regression; found
   BUG-15 (SEV-3 unicode punctuation) + re-confirmed the pre-existing release.yml issue.

## Human tester slot (Karl)

The `command_probe` typed-phrase check (session scenarios 5–9) was OFFERED to Karl and is
**optional** for this no-audio logic feature — the agent-tester submissions above are the
substantive results, and real *spoken* testing occurs in the voice-engine feature's UAT. Karl
made the triage decision (Option A). Any phrasings Karl surfaces from the probe before merge are
folded into the Fix-Now set.

## Disposition

See `test-session-2-v1.md` (completed) and `BUGS.md` (BUG-11..16). SEV-1 + SEV-2 fixed
test-first; SEV-3 deferred to the voice-engine feature / F7. No open SEV-1/2.
