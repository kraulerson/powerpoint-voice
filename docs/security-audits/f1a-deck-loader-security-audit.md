# Security Audit Findings — Feature: F1a Deck Loader (PPTX parse layer)

**Feature:** F1a-deck-loader (OOXML deck loading & slide-model extraction)
**Date:** 2026-08-03
**Auditor Persona:** Senior Security Engineer (5 parallel specialist audit agents)

This feature parses an UNTRUSTED .pptx (arrives by email/USB — attacker-controlled,
threat model §Threat Actors), so it is the highest-risk code surface in the app. The audit
ran the framework's Semgrep pass plus five independent specialist lenses: memory safety,
DoS/resource-exhaustion vs the threat model, confidential-data containment, adversarial
correctness, and dependency-API misuse.

---

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | p/owasp-top-ten, p/security-audit | Pass | 0 findings (8 files, re-run after remediation) |

## Manual Review Findings

All five agents' confirmed findings. Every Critical/High was remediated test-first with a
regression test bound to its ID (`tests/test_deck_loader.cpp`, "SECURITY-AUDIT REMEDIATION").

| # | Category | Finding | Severity | File:Line | Resolution | Status |
|---|----------|---------|----------|-----------|------------|--------|
| F1a-1 | Input Validation | ZIP64 declared size > 2^63 wraps the signed cap comparison, bypassing the per-part/cumulative zip-bomb caps; truncated `int` resize then overflows on read | Critical | deck_loader.cpp `load()` cap loop + `readPart` | Compare UNSIGNED (`st.size > (zip_uint64_t)cap`), check `ZIP_STAT_SIZE` valid bit, guard `readPart`, `resize` uses `qsizetype` | Fixed |
| F1a-2 | Threat Model (DoS) | `descendantLocal` recursed with no depth bound → stack-overflow crash on a deeply-nested hostile deck, on the happy path (background/pic/graphicFrame) | Critical | deck_loader.cpp `descendantLocal` | Rewrote as ITERATIVE (explicit heap stack) — no call-stack growth regardless of nesting | Fixed |
| F1a-3 | Correctness (product-critical) | A missing slide relationship or missing slide part was silently `continue`d, compacting the slide vector → index drift → "go to slide N" lands on the WRONG slide during a live talk | Critical | deck_loader.cpp slide loop | Insert a `placeholder` slide preserving slide numbering + surface a `missing-slide` warning; never drop | Fixed |
| F1a-4 | Threat Model (DoS) | Unbounded shapes/runs per slide — a legal-but-pathological slide with millions of shapes exhausts memory at parse (TM-018 at parse time) | High | deck_loader.cpp `parseSlide` | Added `maxShapesPerSlide` cap (default 5000); stop + warn at the cap | Fixed |
| F1a-5 | Correctness | Missing/non-numeric `sldSz` → slide size 0 → potential divide-by-zero in the F1b renderer | Medium | deck_loader.cpp `load()` sldSz | Emit a `slide-size` warning on non-positive size; F1b must treat non-positive as a safe default (recorded obligation for F1b) | Fixed (parse side); F1b guard tracked |
| F1a-6 | Input Validation | Malformed EMU / font-size attributes silently become 0 via `toLongLong`/`toDouble` (shape jumps to origin / 0pt) | Low | deck_loader.cpp `parseXfrm`/`parseRun` | Accepted for MVP — 0 is a safe, non-crashing default; a future warning on parse-failure is a backlog item | Accepted |
| F1a-7 | Dependency Hygiene | Raw `zip_t*` not RAII-guarded; `st.valid` name bit unchecked | Low | deck_loader.cpp | `st.valid` size+name bits now checked; every return path already `zip_close`s (verified by the API-misuse agent — no leak). RAII wrapper deferred as cosmetic | Accepted |

## Threat Model Cross-Reference

| Threat ID | Relevant to This Feature? | Mitigation Verified? | Notes |
|-----------|--------------------------|---------------------|-------|
| TM-010 (XML entity expansion) | Yes | Yes | pugixml does not process DTDs/entities — structurally not exploitable; invariant guarded by the parser choice (ADR gate on swaps) |
| TM-011 (Confidential data to disk/cache) | Yes | Yes | Containment audit PASSED all four checks: read-only zip, no temp files, no deck text in error strings, no logging, in-memory only |
| TM-013 (Confidential data leak via logs) | Yes | Yes | No qDebug/cout/cerr in the loader; error messages carry only filenames/sizes, never slide text |
| TM-014 (Zip-bomb, cumulative) | Yes | Yes | Cumulative uncompressed cap enforced from the central directory; F1a-1 closed the ZIP64 wrap bypass |
| TM-015 (Zip-slip / path traversal) | Yes | Yes (by design) | Loader never writes to disk — parts read in-memory by name; traversal-named parts cannot escape a filesystem the loader never touches |
| TM-016/021 (Malformed font → code exec) | Partially (F1b) | N/A here | Font bytes are NOT decoded in the parse layer (deferred to F1b render); this feature only records the reference. Font-load hardening is an F1b obligation |
| TM-017 (Decompression/nesting DoS) | Yes | Yes | Per-part + cumulative caps (F1a-1) and the iterative walker (F1a-2) close the parse-time vectors |
| TM-018 (Render bomb) | Partially | Partial | Parse-time shape cap (F1a-4) added; the render-time mitigation (pre-render off-thread + per-slide deadline) is the F1b/Bible §3 obligation |

## Summary

| Status | Count |
|--------|-------|
| Fixed | 5 |
| Accepted (with rationale) | 2 |
| Open | 0 |

**All findings resolved:** Yes

Two Low findings (F1a-6 malformed-attribute defaults, F1a-7 RAII cosmetic) are Accepted with
rationale rather than Open — both are non-crashing and safe; neither blocks. The remaining
five (including all three Criticals and the High) are Fixed with regression tests. Confidential
containment and libzip/pugixml resource-lifecycle audits both passed with zero defects.
