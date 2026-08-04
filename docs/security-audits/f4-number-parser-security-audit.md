# Security Audit Findings — Feature: F4 Number Parser

**Feature:** F4-number-parse (`parseSlideNumber` — "go to slide N")
**Date:** 2026-08-03
**Auditor Persona:** QA/Security reviewer (1 agent) + Semgrep

Pure logic on a `QString` from the recognizer/keyboard — no untrusted binary, no file/network
I/O. The risk is a wrong or crashing parse choosing the wrong slide during a live talk.

---

## Automated Scan Results

| Tool | Config | Result | Findings |
|------|--------|--------|----------|
| Semgrep | p/owasp-top-ten, p/security-audit | Pass | 0 findings (re-run after remediation) |

## Manual Review Findings

| # | Category | Finding | Severity | Resolution | Status |
|---|----------|---------|----------|------------|--------|
| F4-1 | Input Validation | `evalWords` could overflow `int` on flooded input (e.g. many large numerals) → a NEGATIVE slide number to the caller; no token cap | High | Cap at 12 number tokens; bound the running total to ≤100000, returning nullopt on overflow | Fixed |
| F4-2 | Input Validation | `t.toInt()` ignored its `ok` flag in the digit path — a too-large numeral became 0 and could silently mis-parse | Medium | Check `ok`; return nullopt when a numeral cannot be represented | Fixed |
| F4-3 | Correctness | Arithmetic fired on malformed sequences: "fifteen fifteen" → 30, "one ten" → 11 (a recognizer stutter jumping to the WRONG slide) | Medium | Grammar validation in `evalWords`: only single unit/teen or tens-then-unit per group; malformed sequences REJECT (nullopt = no jump + "try again") | Fixed |
| F4-4 | Robustness | Empty / all-filler / 100k-char inputs | Low | Confirmed safe — linear regex split (no catastrophic backtracking), bounded allocation, no crash/hang | Accepted (no defect) |

## Threat Model Cross-Reference

| Threat ID | Relevant? | Notes |
|-----------|-----------|-------|
| TM-002/019 (audience/mis-heard voice → wrong action) | Yes | F4-3 hardening makes the parser REJECT malformed/stutter input rather than guess a wrong slide — reinforces the grammar-constrained-recognizer defense. Range-checking against deck length (out-of-range → "deck has N slides") is the F2/F7 caller's job. |

## Summary

| Status | Count |
|--------|-------|
| Fixed | 3 |
| Accepted (no defect) | 1 |
| Open | 0 |

**All findings resolved:** Yes

The three real findings (negative-overflow, unchecked toInt, malformed-sequence wrong-jump) are
fixed with regression tests. The parser now fails safe: anything it cannot confidently parse
returns nullopt, so a mis-heard or malformed "go to slide N" produces no jump rather than a
wrong one — the correct behavior for a live presentation.
