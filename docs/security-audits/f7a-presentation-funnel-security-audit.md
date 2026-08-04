# Security Audit Findings — Feature: F7a Presentation Funnel

**Feature:** F7a-presentation-funnel (`PresentationController` + the closed `Notice` vocabulary)
**Date:** 2026-08-04
**Auditor Persona:** Senior Security Engineer (adversarial agent, findings reproduced under
ASan+UBSan against the real sources) + Semgrep

The decision core of the presenter: `dispatch(cmd, source, paused)` is the only code that computes a
slide index, and both input paths (voice, keyboard) funnel through it. Pure logic, no I/O.

---

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | p/owasp-top-ten, p/security-audit | Pass | 0 findings |
| ASan + UBSan | Debug, full suite | Pass (after remediation) | 1 signed-overflow UB found and fixed; 3128 assertions clean |

## Manual Review Findings

| # | Category | Finding | Severity | Resolution | Status |
|---|----------|---------|----------|------------|--------|
| HIGH-1 | Access control | `undoJump()` was a SECOND index-computing entry point that checked no gate: it moved the deck **behind the quit overlay** and **behind the privacy blackout**, contradicting the header's own "only code that computes a slide index" claim. Live impact: the presenter blanks the screen on slide 40, un-blanks, and slide 12 is projected | High | `undoJump()` now runs dispatch's preamble — Suppressed under ConfirmQuit; leaves Holding to Presenting rather than silently repositioning behind it | Fixed |
| HIGH-2 | Info disclosure | The privacy blackout (Esc) was dismissed by ANY command — including a **rejected** one, and a **voice "continue presentation" while paused**. An audience member during Q&A could therefore reveal the deck. The pause gate protected the slide INDEX but not the PROJECTED SURFACE (TM-002/012/019) | High | The un-hold is now conditional on an ACCEPTED navigation, evaluated after the command switch. Pause/Continue and rejected commands leave the blackout up | Fixed |
| MED-3 | Availability | `requestHolding(qint64 nowMs = 0)`'s default made the quit prompt self-destruct on the first tick of any real clock (`nowMs - 0 >= 10000` trivially true) — the presenter could not exit cleanly in front of the room | Medium | Default argument removed so no call site can omit the clock; header documents `nowMs` as MONOTONIC | Fixed |
| MED-4 | Robustness / UB | `onTick`'s raw `nowMs - confirmEnteredMs_` was signed-overflow UB on far-apart operands, and a backwards clock (NTP/DST step) pinned the prompt open | Medium | Backwards steps return early; the difference is computed in UNSIGNED arithmetic. **Note:** the first fix (clamping direction only) did NOT remove the UB — UBSan caught it still firing, and the fix was corrected. Verified clean | Fixed |
| MED-5 | Usability / safety | `NoticeId::Resumed` was unreachable by construction: `RecognizerController` clears its pause state BEFORE calling the sink, so `paused` is always false by the time dispatch sees a Continue. BUG-11 (SEV-1) was about the presenter not knowing whether voice is live — that signal was dead | Medium | The notice no longer depends on `paused`; asserted by test | Fixed |
| MED-6 | Info disclosure | `noticeForRole()` had no tests and no callers; the audience/operator split is implemented for 2 of 11 ids, so operator-only text (e.g. `HoldingHint`) would render verbatim to the audience role. TM-012's stated mitigation is "split overlay by display role" | Medium | Deferred to F7b, which introduces the first caller and the display surfaces. Logged in BUGS.md so it cannot be lost | Deferred |
| LOW-7 | Robustness | `setDeck()` resets to slide 1, forces Presenting, and never clears `quitConfirmed_` | Low | Deferred to F7b (deck-reload paths do not exist yet); logged | Deferred |
| LOW-8 | Robustness | A `Command` whose type matched no case produced a SILENT no-op — the hardest failure to diagnose mid-talk | Low | `DispatchResult::outcome` now defaults to `Rejected`, so an unmatched command fails loudly. The switch is deliberately left `default:`-free to keep `-Wswitch` coverage | Fixed |
| LOW-9/10 | Cosmetic | Gate ordering (deck-empty before pause); "Deck has 1 slides"; unused notice args | Low | Deferred to F7b with the notice work | Deferred |
| INFO-11 | Scope | TM-002's specified rate limiting (3/5 s, >=700 ms apart) is absent — `dispatch()` takes no timestamp so it structurally cannot throttle | Info | **Not a defect in this sub-feature:** rate limiting is assertion group F, scheduled for a later F7 stage. Recorded here so the audit trail does not imply TM-002 is fully mitigated | Scheduled |

## Confirmed clean under adversarial attack

- **BUG-16 range safety — could not be broken.** INT_MIN/INT_MAX/0/negative all Rejected with no
  arithmetic overflow; `setDeck(INT_MAX)` does not overflow; only three writers of `current_`, all
  bounded. The predicted deck-reload undo bug is **not present** — `setDeck` clears the undo target,
  and it is the only writer of `slideCount_`, so a stale target cannot outlive its deck.
- **Quit unreachability — could not be broken.** `quitConfirmed_` is assigned in exactly one
  statement behind `mode_ == ConfirmQuit`; no command, mode, ordering or source reaches it. Esc
  auto-repeat cannot walk through the prompt.
- **Pause gate (index half) — could not be broken.** Voice navigation could not be made to move a
  slide while paused; the keyboard could not be made to fail. The presenter cannot be stranded.
- **Information leakage — airtight at the type level.** `Notice` is four trivial members with no
  pointer, string or container; `noticeForRole` builds every string from a compile-time literal with
  `.arg(int)` only. A caller cannot inject text: deck content, file paths and heard speech have no
  channel (Bible §8, TM-012/013). The residual is role-policy completeness (MED-6), not an open channel.

## Threat Model Cross-Reference

| Threat ID | Relevant? | Notes |
|-----------|-----------|-------|
| TM-002 / TM-019 (audience or mis-heard voice → wrong action) | Yes | HIGH-2 was a live instance: voice could dismiss the privacy blackout during Q&A. Fixed. Rate limiting (INFO-11) remains scheduled. |
| TM-012 / TM-013 (Confidential content reaching a display or log) | Yes | HIGH-2 fixed the surface-level leak; the closed Notice vocabulary makes the content-level leak structurally impossible. MED-6 (role policy) deferred to F7b. |
| TM-018 (availability during a live talk) | Yes | MED-3/MED-4 both broke the exit path on a real clock. Fixed and sanitizer-verified. |

## Summary

| Status | Count |
|--------|-------|
| Fixed | 6 |
| Deferred (logged in BUGS.md) | 4 |
| Scheduled (later F7 stage) | 1 |
| Open | 0 |

**All findings resolved:** Yes

(No open items. Deferred items are Medium/Low, belong to surfaces that do not exist yet in F7a, and
are recorded in BUGS.md so the Phase 2→3 gate will see them.)

The two safety properties this sub-feature exists to provide held under adversarial attack — a bad
slide number can never move the deck, and no command can end the presentation. The two High findings
were both *gate-bypass* defects around those properties rather than failures of them: a second entry
point that skipped the gates, and a blackout that the wrong events could dismiss. Notably, the first
attempt at the MED-4 fix was incomplete and only the sanitizer run proved it — a reminder that
"fixed" means "verified", not "edited".
