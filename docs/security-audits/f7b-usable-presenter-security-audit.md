# Security Audit Findings — Feature: F7b Usable Presenter

**Feature:** F7b-usable-presenter (off-thread deck load + pre-render, display geometry, key
translation, the presentation window, and the AppShell wiring)
**Date:** 2026-08-04
**Auditor Persona:** Senior Security Engineer (adversarial agent). **Every finding was reproduced
by compiling and running the code** — probe programs, ASan, and ThreadSanitizer — none is
speculative.

---

## Automated Scan Results

| Tool | Config | Result |
|------|--------|--------|
| Semgrep | p/owasp-top-ten, p/security-audit | 0 findings |
| ThreadSanitizer | full suite + a dedicated race probe (8 rounds x 120 slides, GUI thread hammering queued setCurrentIndex while reading rasters_) | **0 races**, 3722 assertions |
| ASan | targeted probes | used to pin C1/C4 to exact frames |

## The structural finding (most important)

**`AppShell` had ZERO test coverage.** All 178 passing tests exercise the pure leaves; every one of
the five Critical findings lived in the *wiring* that no test touched. This is the same shape as
walk finding ISSUE-017: the process drives excellent testing of decomposed units and never asks
whether the assembled product works. Recorded as such.

## Critical findings — all fixed and re-verified against their own reproductions

| # | Finding | Fix | Status |
|---|---------|-----|--------|
| C1 | **Use-after-free → SEGV on the first arrow key after pre-render.** `QThread::finished → deleteLater` fires ON the worker thread with the worker as a direct receiver, so Qt destroyed the workers the moment pre-render completed; `AppShell`'s raw pointers dangled, and `showSlide()`'s `invokeMethod` dereferenced the freed vptr. Reproduced as SIGSEGV in an optimised build, and as an ASan heap-use-after-free on every clean quit | `QPointer` for both workers — it self-nulls, so every `if (worker_)` guard becomes truthful | Fixed |
| C2 | **A 0-slide deck produced an UNQUITTABLE fullscreen black window.** `setDeck(0)` → `Mode::Idle`; `requestHolding` is a no-op in Idle, so Esc can never reach ConfirmQuit, `confirmQuit` requires ConfirmQuit, and `closeEvent` refuses without it. Only Force Quit could recover the projector | Refuse before entering fullscreen: report "that file contains no slides" and return to the start view | Fixed |
| C3 | **The deck's full file path was rendered into a QMessageBox** that is parented to nullptr and therefore lands on the primary screen — the projector in a mirrored stage setup. Bible section 8 / TM-013 | `describeLoadError(kind)` returns a fixed per-kind string; `LoadError::message` is never displayed | Fixed |
| C4 | **Attacker-triggered use-after-free whose leaked heap bytes were printed into that same dialog.** `deck_loader.cpp` called `zip_close(za)` and *then* read `st.name`, which libzip had freed. A hostile .pptx declaring an over-cap part made the app print freed heap — which at that point holds the deck path and parsed slide text | Copy the part name into a `QString` BEFORE `zip_close` | Fixed |
| C5 | **Media-part read amplification: ~640 GB allocation ceiling from a ≤200 MB file.** Each `<p:pic>` re-read its media entry while the cumulative decompression cap — which walks the central directory — counted the entry once. Measured: a 63 MB file with 40 references reached 2.4 GB resident, perfectly linear | Cache media parts by name for the load (QByteArray is copy-on-write, so sharing is free) and charge cumulative reads against `maxTotalUncompressed` | Fixed |

## High findings

| # | Finding | Resolution | Status |
|---|---------|-----------|--------|
| H2 | Shutdown was not bounded: `quit()` cannot interrupt the render loop (it is not an event loop), and the `QThread`s were `AppShell` children destroyed while running — a `qFatal` abort. Combined with H1 (a legal slide can take minutes) quitting was a guaranteed crash | `wait(5000)`, then `terminate()` + `wait(1000)`, then `deleteLater()` the threads | Fixed |
| H3 | **The privacy blackout was never rendered.** `Mode::Holding` was entered correctly but nothing blanked the surface — Esc left the confidential deck on the projector. `surfaceStateFor`/`HoldLastGood` was dead code with zero callers | `refresh()` paints from the current mode; Holding blanks the surface and shows the hint | Fixed |
| H4 | **The quit prompt was invisible and never timed out**, while swallowing every key — it read as a frozen app. `PresentationController::onTick` had zero callers, and `requestHolding(0)` stamped time 0 so the timeout could never fire | The prompt renders; a `QTimer` drives `onTick` with a monotonic `QElapsedTimer` | Fixed |
| H5 | `Ctrl+Shift+D/F/R` were produced and silently dropped, and the window never chose a screen — if fullscreen opened on the laptop panel the documented escape hatch did nothing | An external (non-primary) screen is preferred at first show; `MoveSlideWindowToNextScreen` is implemented | Fixed |
| H1 | **TM-018 PREVENT measures the wrong quantity.** Caps count shapes and text runs only. Measured under-cap bombs: 2000 pictures of one 31 Mpx image ≈ **309 s**; 5000 runs × 300k chars ≈ **657 s**. ISOLATE holds (the UI never blocks) but the slide never appears | **Deferred to F7c** (BUG-21), which lands the full four-cap set per the ratified TM-018.3-A. Needs total characters and total *declared* image pixels added to `SlideComplexity` — both model-only and unit-testable | Deferred |
| H6 | `rasters_` is unbounded: 3840×2160 RGB32 ≈ 31.6 MB/slide × 300 = **9.27 GB** | **Deferred to F7c** (BUG-22) — this is precisely the gap the ratified always-on 2 GB window closes. Auditor's live judgement: survivable below ~120 slides at 4K; above that, clamp the render target to 1080p as a one-line stopgap | Deferred |

## Medium / Low

| # | Finding | Status |
|---|---------|--------|
| M1 | The typed-number 3 s staleness rule and `onModeChanged` were both dead (`nowMs` hardcoded to 0, zero callers) — a digit typed minutes earlier silently prefixed the next one | Fixed (monotonic clock + mode-change wiring) |
| M2 | Operator-role notices ("Slide 2 (from 1)") were painted on the audience-facing fullscreen window | Fixed (audience role until F5 adds an operator surface) |
| M5 | The whole deck file was `readAll()`-ed for a hash *before* any size cap applied | Fixed (gate on size first) |
| L3 | `start()` is a public slot and `processEvents()` could re-enter it, resetting `done_`/`emitted_` mid-run | Fixed (re-entrancy guard) |
| M3 | `isPlaceholder` is discarded, so a last-resort 1×1 raster renders as a black square with no notice | Deferred (BUG-23) |
| M4 | `HoldLastGood` is never used, so jumping to an un-rendered slide flashes the projector black | Deferred (BUG-23) |
| L1 | `std::move` on a `const LoadResult` selects the copy constructor | Deferred (BUG-23) |
| L2 | `window_`/`start_` are parentless and never deleted | Deferred (BUG-23) |
| L4 | `paused` was hardcoded false | Partly fixed (`setPaused` plumbed); the caller arrives with the voice engine |

## Confirmed unbreakable under attack

- **The QPixmap amendment holds absolutely.** `grep -rn QPixmap src/` returns only two *comments*; no `QPixmap` exists in the product. `QImage` (implicitly shared, atomic refcount) crosses the queued connection. No path touches `QPixmap` off the GUI thread.
- **ThreadSanitizer clean** across the suite and a dedicated race probe. `cancelled_`/`current_` are correctly atomic; `deck_`/`target_`/`renderFn_` are written only before `moveToThread`+`start` (happens-before via thread start); `done_`/`emitted_` are worker-thread-only.
- **PREVENT is airtight for what it measures** — no path reaches `renderFn_` for an over-cap slide, including the null-image fallback. The weakness is the metric (H1), not the mechanism.
- **Degenerate inputs handled:** null deck, 0-slide deck, `slideWidth/Height == 0` with a 0×0 target — a valid placeholder, no division by zero.
- **The Notice vocabulary is a genuinely closed channel.** Every string is built from an enum id plus two ints; there is no parameter through which slide text could reach a display. With zero `qDebug`/`qWarning`/file writes in `src/`, **slide text cannot escape**. The only section 8 leaks were the file path (C3) and freed heap (C4) — both via `LoadError::message`, both closed.

## Threat Model Cross-Reference

| Threat | Notes |
|--------|-------|
| TM-018 (mid-deck render bomb) | ISOLATE and PREVENT verified; the metric gap (H1) is deferred to F7c with the full cap set |
| TM-012 / TM-013 (Confidential content disclosure) | C3 and C4 were live disclosure channels to a projector-facing dialog; both closed. H3 (the blackout not blanking) was a disclosure control that did not work; fixed |
| TM-014/015 (hostile archive) | C4 and C5 were both reachable from a crafted .pptx; both closed |
| TM-002/019 | H3 restores the privacy control Esc is supposed to provide |

## Summary

| Status | Count |
|--------|-------|
| Fixed | 13 |
| Deferred (logged BUG-21..23) | 7 |
| Open | 0 |

**All findings resolved:** Yes

(No open items. The deferred set is H1/H6 — both explicitly scheduled to F7c, where the ratified
TM-018.3-A cap set and the 2 GB cache window land — plus four Medium/Low polish items, all logged in
BUGS.md so the Phase 2→3 gate sees them.)

The audit's own conclusion is worth preserving: the decomposed, pure layers could not be broken, and
every Critical lived in the untested wiring between them. That is a finding about the *process*, not
just this code, and it is recorded in the walk log.
