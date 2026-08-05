# Archived Test Results

Completed UAT sessions and scan results are archived here for the permanent record;
the live working files stay under `tests/uat/sessions/<date>-session-N/`.

**Naming:** `[date]_uat-session-N-vX.md` for UAT sessions (Markdown, not HTML: these
sessions used the documented Markdown fallback because the features under test had no
clickable UI — see each session's template note), and
`[date]_[scan-type]_[pass|fail].[ext]` for Phase 3 scans.

| Archived | Session | Features under test | Outcome |
|---|---|---|---|
| `2026-08-03_uat-session-1-v1.md` | UAT 1 | F1a deck loader, F1b slide renderer | 7 real-deck bugs found (2 SEV-1, incl. invisible text), all fixed |
| `2026-08-04_uat-session-2-v1.md` | UAT 2 | F4 number parser, F2/F3 command layer | Safety property confirmed unbreakable; SEV-1 stuck-in-Paused + SEV-2 phrasing fixed |
| `2026-08-05_uat-session-3-v1.md` + `-triage.md` | UAT 3 | F7a funnel, F7b usable presenter | **The first session on a presentable build.** 5 agent-testers, 24 ranked findings, 26 confirmed-working items. 1 SEV-1 + 4 SEV-2 fixed (blackout un-blanking itself; double letterboxing on a projector; teardown abort; grpSp stack overflow; CLI failed-open leaving no window). Archived same-day — the ISSUE-020 lesson applied |

**Note on why these were archived late (2026-08-04).** Both sessions were completed and
their working artefacts committed at the time, but neither was archived here until Karl
asked whether UAT results were being logged as required. The archive step is mandated by
the project CLAUDE.md ("After completion and review, archive to `docs/test-results/`")
but is **not** one of the nine enforced `uat_session` checklist steps, so completing the
gated process does not produce it — recorded as walk finding ISSUE-020.
