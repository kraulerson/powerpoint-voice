# Bug Tracker

<!--
  This file tracks bugs found during UAT sessions and ad hoc testing.
  Status and severity patterns are read by scripts/test-gate.sh for phase gate checks.
  Do NOT change the table format — the column order and status values are parsed by scripts.
-->

| # | Severity | Status | Feature | Description | Session | Disposition | Fix Reference | Verified In |
|---|---|---|---|---|---|---|---|---|
| 1 | SEV-1 | Fixed | F1a/F1b | Text color read only from `<a:srgbClr>`; real decks use theme/scheme colors (`<a:schemeClr>`) or inherit from the master → no color → black default → INVISIBLE text on dark exec slides | Session 1 | Fix Now | theme parse + schemeClr resolve + luminance default | Session 1 |
| 2 | SEV-1 | Fixed | F1a | Placeholder text with no inline `<a:xfrm>` (position inherited from slideLayout/master) collapses to 0,0,0,0 → boxes overlap top-left and cx=0 clips text away | Session 1 | Fix Now | slideLayout placeholder geometry + default content-rect fallback | Session 1 |
| 3 | SEV-2 | Fixed | F1a/F1b | Grouped shapes `<p:grpSp>` treated as Unsupported → grey placeholder box hides all grouped text/content | Session 1 | Fix Now | recurse into grpSp children | Session 1 |
| 4 | SEV-2 | Fixed | F1b | Long text does not wrap (`Qt::TextSingleLine`) → bullets silently truncated at box edge, no ellipsis | Session 1 | Fix Now | QTextLayout WordWrap | Session 1 |
| 5 | SEV-2 | Fixed | F1b | Multi-run paragraph renders in the first run's color/font only; per-run color/bold/italic/size of later runs lost | Session 1 | Fix Now | QTextLayout per-run FormatRange | Session 1 |
| 6 | SEV-2 | Fixed | F1a | Line breaks `<a:br>` dropped; runs concatenate ("Q3⏎FY26" → "Q3FY26") | Session 1 | Fix Now | `<a:br>` → U+2028 separator | Session 1 |
| 7 | SEV-3 | Fixed | F1a/F1b | Bullet markers (`buChar`/`buAutoNum`) and list-level indentation ignored | Session 1 | Fix Now | parse buChar/lvl + render bullet prefix/indent | Session 1 |

**Known remaining limitations (documented, not blocking, tracked for post-MVP):** theme resolution uses `theme1.xml` and the default color map (no per-master `clrMap` override); placeholder geometry inherits one level (slideLayout), not slideMaster; group child-coordinate transforms are not applied (positions read from child xfrms); bullets render inline markers only (master list-style bullets not inherited). These are refinements — text is now **readable and correctly colored** on real decks.
<!--
  Severity: SEV-1, SEV-2, SEV-3, SEV-4 (see PROJECT_BIBLE.md Bug Severity Classification)
  Cite bugs elsewhere as BUG-<#> (the # column, bare integer, no zero-padding — see docs/IDENTIFIERS.md)
  Status: Open, Fixed, Deferred, Won't Fix, Post-MVP, Removed
  Disposition: Fix Now, Defer, Won't Fix, Post-MVP (assigned during triage, Step 2.8)
  Session: UAT session number where the bug was found (e.g., "Session 4")
  Fix Reference: PR number or commit hash of the fix (e.g., "PR #12" or "abc1234")
  Verified In: UAT session number where the fix was verified (e.g., "Session 5")
-->

## Status Guide

| Status | Meaning |
|---|---|
| **Open** | Bug confirmed, not yet fixed |
| **Fixed** | Fix implemented and verified |
| **Deferred** | Tracked with justification — must be resolved or feature removed at Phase 2→3 gate |
| **Won't Fix** | Accepted as-is with documented rationale (SEV-3/4 only) |
| **Post-MVP** | Moved to post-MVP backlog (SEV-4 enhancements only) |
| **Removed** | Feature containing the bug was removed |

## Severity Guide

| Severity | Definition | Examples | Can Defer? |
|---|---|---|---|
| **SEV-1** | Data loss, security breach, app crash on core flow | Auth bypass, database corruption, crash on login | No — must fix immediately |
| **SEV-2** | Feature broken but workaround exists, significant UX failure | Form submits wrong data, layout broken on one platform | Yes — but must resolve or remove feature at Phase 2→3 gate |
| **SEV-3** | Minor UX issue, cosmetic, non-core edge case | Alignment off, tooltip truncated, rare edge case | Yes |
| **SEV-4** | Enhancement, suggestion, polish | "Would be nice if...", performance optimization | Automatic Post-MVP |
