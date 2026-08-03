# Security Audit Findings — Feature: F1b Slide Renderer

**Feature:** F1b-slide-render (slide model → QImage; + image-bytes loading in F1a)
**Date:** 2026-08-03
**Auditor Persona:** Senior Security Engineer (2 parallel specialist audit agents + Semgrep)

The renderer operates on the already-validated slide model, so its main new untrusted surface
is **decoding attacker-supplied image bytes** (`QImage`/`QImageReader`) and rendering
attacker-influenced geometry/typography. The audit ran two lenses — untrusted image decode,
and render-time DoS/correctness — plus the framework Semgrep pass.

---

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | p/owasp-top-ten, p/security-audit | Pass | 0 findings (re-run after remediation) |

## Manual Review Findings

| # | Category | Finding | Severity | File | Resolution | Status |
|---|----------|---------|----------|------|------------|--------|
| R1 | Input Validation / DoS | An unbounded declared font size (or a 1-EMU slide width inflating scale) made `static_cast<int>(pxSize)` undefined and rasterized a giant glyph bitmap → hang/OOM mid-presentation | Critical | slide_renderer.cpp | `clampFontPx()` clamps every `setPixelSize` to [1, slide height] before the cast | Fixed |
| R2 | Untrusted Decode | `QImage::fromData` auto-dispatched to ANY bundled image plugin (TIFF/WebP/GIF — the CVE-prone codec class, TM-016) on attacker bytes | High | slide_renderer.cpp | `decodeGuarded()` uses `QImageReader` and decodes only allow-listed PNG/JPEG; other formats → placeholder | Fixed |
| R3 | DoS | Decoded pixel dimensions were bounded only by Qt's implicit 256 MiB default — a small image can decode to gigabytes; a deck of many large images OOMs | Medium | slide_renderer.cpp | `QImageReader::setAllocationLimit(128)` + a 40 Mpx dimension check before `read()` | Fixed |
| R4 | Correctness / Containment | No clip rect — content at attacker-controlled negative/oversized geometry bled into the black letterbox bars | Medium | slide_renderer.cpp | `p.setClipRect(slideRect)` confines all painting to the slide | Fixed |
| R5 | DoS | The per-slide shape cap did not bound paragraphs/runs/text length within a text box — one box could carry millions of paragraphs or a gigabyte of text | High | deck_loader.cpp | Added `maxParagraphsPerBox`/`maxRunsPerParagraph`/`maxRunTextChars` caps | Fixed |
| R6 | Correctness (advisory) | Render output is deterministic on the same machine (tested) but NOT across machines/font sets — a pre-render cache keyed only on deck content could serve wrong pixels across machines | Low | — | Accepted — the app renders on one machine per run; the pre-render cache (F7) is per-session in-memory, not shared across machines. Recorded for F7 cache-key design | Accepted |
| R7 | Robustness | A non-positive target size produced a null/invalid image without an explicit guard | Low | slide_renderer.cpp | Early return for `targetW<=0 || targetH<=0` | Fixed |

## Threat Model Cross-Reference

| Threat ID | Relevant? | Mitigation Verified? | Notes |
|-----------|-----------|---------------------|-------|
| TM-016/021 (Malformed media → decoder exploit) | Yes | Yes | R2 allow-lists PNG/JPEG only; R3 bounds decode. Font engine (QRawFont) not directly invoked — QFont via the platform DB; huge-glyph DoS closed by R1 |
| TM-018 (Render bomb / mid-talk stall) | Yes | Partial | R1 (font), R3 (image size), R5 (text volume), F1a-4 (shape count) bound parse+render cost. The remaining piece — pre-render OFF the UI thread at load with a per-slide deadline — is the F7 presentation-controller obligation (Bible §3), not the pure render function |
| TM-011/013 (Confidential data containment) | Yes | Yes | Renderer writes nothing to disk and logs nothing; operates in-memory on the model |

## Summary

| Status | Count |
|--------|-------|
| Fixed | 6 |
| Accepted (with rationale) | 1 |
| Open | 0 |

**All findings resolved:** Yes

Six findings fixed with regression tests (R1/R2/R4/R5 have dedicated tests; R3/R7 are covered
by the R2 decode-path test and the non-positive-size test). R6 (cross-machine determinism) is
Accepted — it does not affect single-machine correctness and is recorded as an F7 cache-key
design input. The off-thread pre-render orchestration (TM-018 completion) is explicitly F7's
responsibility; the pure render function is bounded in font size, image size, text volume, and
element count.
