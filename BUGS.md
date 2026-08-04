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

| 8 | SEV-1 | Fixed | F1a/F1b | Runs with no inline `sz` rendered at 0→1px (blank/tiny text) — real decks inherit font size from the master `<p:txStyles>` (title/body/other per level) | Session 1 (real deck) | Fix Now | parse master txStyles + resolve default size by placeholder type/level + fallback | Session 1 |
| 9 | SEV-2 | Won't Fix (MVP) | F1b | EMF and WDP (JPEG-XR) images render a "missing image" placeholder — Qt has no decoder for these Windows vector/HD-Photo formats | Session 1 (real deck) | Post-MVP | none feasible without a metafile engine; **workaround: re-export EMF/WDP images as PNG in PowerPoint** | — |
| 10 | SEV-3 | Open | F1b | Image drawn stretched-to-fill its frame; a frame whose aspect differs from the image looks squished (matches PowerPoint's fill default in most cases; a source-crop/`srcRect` is not applied) | Session 1 (real deck) | Defer | investigate `srcRect` / aspect after the font-size fix re-check | — |

**Known remaining limitations (documented, not blocking, tracked for post-MVP):** theme resolution uses `theme1.xml` + default color map; placeholder geometry inherits one level (slideLayout); group child transforms not applied; inline bullets only; **EMF/WDP images unsupported (BUG-9 — re-export as PNG)**; font-size inheritance resolves via the master txStyles (not per-layout overrides). Text is now **readable and correctly sized/colored** on real decks.
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
