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

| 11 | SEV-1 | Fixed | F2/F3 | Stuck-in-Paused: once paused for Q&A, ONLY the literal "continue presentation" resumes; "resume", "continue", "continue the presentation", "okay let's continue" are all dropped while nav stays gated → deck frozen mid-talk with no voice recovery | Session 2 | Fix Now | resume synonyms ("resume"/"continue"/±"the presentation") in matchCommand, test-first | Session 2 |
| 12 | SEV-2 | Fixed | F2/F3 | Strict equality on the 4 fixed commands drops common natural phrasings — "okay next slide", "next slide please", "so previous slide", "pause the presentation", "lets pause" → (no command); plus a "please" asymmetry (works for "go to slide N please" but not the fixed commands) | Session 2 | Fix Now | small SAFE leading/trailing filler strip + optional "the" on control phrases; re-audited (audience sentences still no-command), test-first | Session 2 |
| 13 | SEV-3 | Deferred | F2/F3 | Directional aliases miss: "go to the next slide", "move to the next slide", "can we go to the next slide" → (no command). Deferred to the voice-engine feature: collision-prone with audience speech, best designed with the grammar-constrained recognizer | Session 2 | Defer | voice-engine feature (design with Vosk) | — |
| 14 | SEV-3 | Deferred | F4 | Natural spoken numbers miss/surprise: ordinals ("twenty-fifth", "the fifth slide"), "one oh five" (oh=zero), "last" → (no command); and digit-by-digit "go to slide five and six" → GoToSlide(56). Fails safe (needs the literal "go to slide" prefix) | Session 2 | Defer | voice-engine feature | — |
| 15 | SEV-3 | Deferred | F2/F3 | The M-MED-1 edge-punctuation strip covers only ASCII; typographic "…" (U+2026) / curly quotes (macOS/iOS dictation auto-substitutes) are not stripped → "next slide…" → (no command). Fails safe; identical on macOS/Ubuntu | Session 2 | Defer | voice-engine feature (broaden to Unicode punctuation) | — |
| 16 | SEV-3 | Deferred | F2/F3, F4 | Matcher emits out-of-range/degenerate slide numbers ("go to slide zero"→0, "-5"→5, digit path bypasses the word path's kMax=100000) — leans entirely on the F7 caller to clamp. F7 MUST range-check 0/negative/over-length before ship (per the F4 caller-range-checks contract) | Session 2 | Defer | F7 clamp requirement + F4 digit-path cap consistency | — |
| 17 | SEV-2 | Fixed | F2/F3 | Bare single-word commands ("resume"/"continue"/"pause") were accepted, so ONE conversational word re-armed navigation during Q&A — the exact window the Paused state protects (TM-002/019). The BUG-11 filler strip made it worse: "okay lets continue", "and now continue", "lets pause" all reduced to a lone command word and fired. Regression introduced by the BUG-11/12 UAT-2 remediation | Design review (voice-engine) | Fix Now | require the object: "pause/continue/resume (the) presentation"; bare words rejected. 7 regression assertions. Residual stuck-in-Paused risk covered by F6 keyboard parity, resequenced BEFORE the voice engine | Session 3 (pending) |
| 18 | SEV-2 | Open | F7 | No presentation UI exists: the app opens a dark StartView and cannot open a deck, display a slide, or be navigated. Loader/renderer/command logic are built and tested but wired to nothing — there is no usable presenter 6 days before the talk | Design review (voice-engine) | Fix Now | F7 presentation UI (current Build Loop), then F6 keyboard parity | — |
| 19 | SEV-3 | Deferred | F7 | Audit F7a deferred items: `noticeForRole()` audience/operator split implemented for only 2 of 11 notice ids (TM-012's mitigation is "split overlay by display role"), and it has no caller or tests yet; `setDeck()` resets to slide 1, forces Presenting and never clears `quitConfirmed_`; cosmetic notice defects ("Deck has 1 slides", unused args, gate ordering) | Design review (F7a audit) | Defer | F7b — the sub-feature that introduces the display surfaces and the first caller | — |
| 20 | SEV-3 | Deferred | F7 | TM-002's specified command rate limiting (3 per 5 s, >=700 ms apart) and reverse-direction confirmation are absent: `dispatch()` takes no timestamp so it structurally cannot throttle. The recorded-playback replay path is unthrottled. Not a defect in F7a — it is assertion group F, scheduled for a later F7 stage — but logged so the audit trail does not imply TM-002 is fully mitigated | Design review (F7a audit) | Defer | later F7 stage (rate-limiting group) | — |
| 21 | SEV-2 | Deferred | F7 | TM-018 PREVENT measures the wrong quantity: the caps count shapes and text runs only, so under-cap slides still take minutes (measured: 2000 pictures of one 31 Mpx image ~309 s; 5000 runs x 300k chars ~657 s). ISOLATE holds so the UI never blocks, but the slide never appears | F7b audit | Defer | F7c — add total characters and total DECLARED image pixels to SlideComplexity, completing the ratified four-cap set (TM-018.3-A) | — |
| 22 | SEV-2 | Deferred | F7 | The raster cache is unbounded: 3840x2160 RGB32 ~31.6 MB/slide x 300 slides = ~9.27 GB, which would swap then OOM-kill the showtime machine. Auditor judgement: survivable under ~120 slides at 4K. **Karl's deck is 10 slides (~316 MB) — NOT a risk for the 2026-08-10 talk**; this is hardening for arbitrary decks | F7b audit | Defer | F7c — the ratified always-on 2 GB window (Bible section 3 A3-1(3) / B1-A). STOPGAP if needed: clamp renderTargetPolicy to 1080p (~7.9 MB/slide, ~2.3 GB at 300 slides) | — |
| 23 | SEV-3 | Deferred | F7 | F7b audit polish set: isPlaceholder is discarded so a last-resort 1x1 raster renders as a black square with no notice (M3); SurfaceState::HoldLastGood is never used so jumping to an un-rendered slide flashes the projector black (M4); std::move on a const LoadResult selects the copy ctor (L1); window_/start_ are parentless and never deleted (L2) | F7b audit | Defer | F7c | — |
| 24 | SEV-1 | Fixed | F7 | Esc privacy blackout un-blanked ITSELF: AppShell::onSlideReady called showSlide() with no mode check, so the pre-render worker painted the Confidential deck back onto the blanked projector 1-3 s after Esc (measured: 56.2% of projector = deck pixels while mode==Holding) and also wiped the quit prompt. The earlier H3 fix gated refresh() but left the raster path ungated — an INCOMPLETE fix | Session 3 | Fix Now | onSlideReady now calls the mode-aware refresh(); showSlide() early-returns unless Presenting; 2 regression tests incl. the first AppShell-level coverage | Session 3 |
| 25 | SEV-2 | Fixed | F7 | Double letterboxing on MacBook+projector: renderTargetPolicy picks the largest screen by DEVICE pixels (Retina laptop 3024x1964, aspect 1.54) over the projector, SlideRenderer bakes bars into the raster at that aspect, and the surface boxes it AGAIN against the 16:9 window -> the deck covered 75% of the projector with 13% smaller text for the whole talk. Invisible in rehearsal (needs a second screen of a different aspect) | Session 3 | Fix Now | new renderTargetForDeck(): picks the presentation (non-primary) screen and fits the DECK's aspect, so the first letterbox is a no-op; 3 regression tests | Session 3 |
| 26 | SEV-2 | Fixed | F7 | teardownWorkers() could SIGABRT or deadlock when a slide render outlives the 5 s wait — destroying a running QThread is a qFatal abort, and terminate() can strand allocator locks. The earlier H2 fix still risked it | Session 3 | Fix Now | if the wait expires, DETACH and deliberately leak the thread (parent cleared) — a leaked thread at shutdown is strictly better than aborting in front of a room | Session 3 |
| 27 | SEV-2 | Fixed | F1a | Unbounded <p:grpSp> recursion stack-overflowed the load worker: a 5.7 KB hostile deck killed the app instantly with no dialog and no stderr | Session 3 | Fix Now | kMaxGroupDepth=32; content below the cap is dropped with a LoadWarning rather than followed | Session 3 |
| 28 | SEV-2 | Fixed | F7 | A failed open on the CLI launch path left NO window and the app exited silently — the audit-C3 'never leave the presenter with no window' guard was dead code there (start_ is null on that path) | Session 3 | Fix Now | call showStart(), which CREATES the view, instead of the dead `if (start_)` guard | Session 3 |
| 29 | SEV-3 | Deferred | F7 | UAT-3 SEV-3/SEV-4 set (19 items), full detail in tests/uat/sessions/2026-08-05-session-3/submissions/uat-3-triage.md: Notices never expire: NoticeClass::Transient is set on every notice and read by nothing, so a message band sits on the projector for the rest of the talk; Ctrl+Shift+F (ToggleFullScreen) and Ctrl+Shift+R (ReRenderDeck) are consumed by the key translator and then silently dropped by AppShell; Typed slide numbers give the presenter zero feedback, and the translator's 'slide number too long' notice has no route to any surface; The privacy-blackout screen shows the audience an operator instruction, and the instruction is factually wrong ('any key to resume'); Keyboard focus is not restored to the presentation window after the modal error dialog closes, and the dialog is unparented; 8 of the 178 ctest entries execute ZERO test cases — and the 4 real cases they should run cover the blackout, the two-step quit, BUG-16 and BUG-11/17; The 1 GB decompression cap is charged per zip entry but slide/... | Session 3 | Defer | F7c and later sub-features; none blocks the 2026-08-10 talk on a 10-slide deck | — |

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
