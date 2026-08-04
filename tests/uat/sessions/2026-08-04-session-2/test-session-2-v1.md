# UAT Test Session — 2

**Date:** 2026-08-04
**Features Under Test:** F4 (Slide-Number Parser), F2/F3 (Voice-Command Grammar & Dispatch)
**Tester:** Karl Raulerson (human) + 3 parallel agent-testers

> **Template note:** Markdown fallback used (not the HTML template). Both features under test
> are **pure command logic** with no clickable UI yet (the presentation UI is F7, and the
> microphone/speech engine is the next feature). So this session tests the command grammar via
> the automated suite, sanitizers, adversarial agent exploration, and a new **typed-phrase
> probe** (`command_probe`) that lets you exercise the grammar by typing the phrases you'd
> actually say. The HTML template + scenario lint apply to end-user UI scenarios (post-F7).

---

## Before you start (test environment)

- **System under test:** macOS (Darwin, Apple Silicon) — the showtime machine. Ubuntu also
  validated via CI. Library `pptv_core` + the headless `command_probe` tool.
- **Project root:** `/Users/karl/Documents/Claude Projects/powerpoint-voice/powerpoint-voice`
- **One-time build:**
  ```
  export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
  export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
  export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build --target command_probe
  ```
- **How to use the probe:** pass each phrase as a quoted argument; it prints the command each maps
  to (or `(no command)`):
  ```
  ./build/command_probe "next slide" "go to slide fifteen" "so let's move on"
  ```

---

## Test Scenarios

### F4 + F2/F3 — Automated (agent-run)

| # | Scenario | Steps | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|---|
| 1 | Full unit/integration suite | `bash scripts/run-tests.sh` | all pass | **Pass** | 86/86 at test time; **90/90 after remediation** (4 new regression tests) |
| 2 | Memory safety | ASan+UBSan build, run suite | No sanitizer errors | **Pass** | 354 assertions, zero ASan/UBSan errors |
| 3 | Semgrep SAST | `semgrep p/owasp-top-ten p/security-audit src/command/` | 0 findings | **Pass** | 0 findings (re-run after remediation) |
| 4 | Test flakiness | run `ctest` 3× | deterministic | **Pass** | 4 runs, all identical |

### F2/F3 — Command grammar (HUMAN — Karl, ~5 min with the probe)

Run this and read the output:
```
./build/command_probe \
  "next slide" "previous slide" "pause presentation" "continue presentation" \
  "go to slide 5" "go to slide fifteen" "go to slide twenty three" \
  "Next slide." "  previous   slide " \
  "so let's move to the next slide in our roadmap" "let's pause here for questions" \
  "next" "banana" "go to slide" "go to slide banana"
```

| # | Scenario | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|
| 5 | The 5 commands map correctly | rows 1–7 print NextSlide / PreviousSlide / PausePresentation / ContinuePresentation / GoToSlide(5) / GoToSlide(15) / GoToSlide(23) | _pending Karl_ | the core "does it understand my commands" |
| 6 | Case + punctuation + spacing tolerated | "Next slide." → NextSlide; "  previous   slide " → PreviousSlide | _pending Karl_ | dictation/keyboard robustness |
| 7 | **Sentences do NOT false-fire** | "so let's move to the next slide…", "let's pause here for questions" → **(no command)** | _pending Karl_ | the audience-Q&A safety property — must NOT jump |
| 8 | Garbage / partial → no command | "next", "banana", "go to slide", "go to slide banana" → (no command) | _pending Karl_ | fail-safe: never a wrong jump |
| 9 | **Your own phrasing** | Type 3–5 phrases the way YOU'll actually speak them on stage (e.g. "go to slide twenty-five", "okay, next slide"). Note any that surprise you | _pending Karl_ | catches "the grammar doesn't match how I talk" |

### F2/F3 + F4 — Exploratory / adversarial (agent-run)

| # | Scenario | Expected Result | Pass/Fail | Notes |
|---|---|---|---|---|
| 10 | Malicious/chaotic input: force a false command, crash, or hang | no false command; no crash | **Pass** | could NOT force a false command (homoglyphs, bidi, embedded keywords) or crash (~23MB/floods clean). Safety property CONFIRMED |
| 11 | Realistic missed commands (how a presenter actually speaks) | flag any natural phrasing that fails | **Fail → fixed** | → BUG-11 (SEV-1 stuck-in-Paused), BUG-12 (SEV-2 filler); BUG-13/14 deferred |
| 12 | Cross-platform (Ubuntu CI) + F4 integration + renderer regression | CI green; numbers correct; no renderer regression | **Pass** | Ubuntu CI green; normalization locale-independent (verified under tr_TR); no regression. → BUG-15 (SEV-3 unicode punct, deferred) |

---

## Bugs Found

Consolidated from the 3 agent-tester submissions (`agent-results/`). Full detail in `BUGS.md`.
Triage decision (Karl, Option A): fix the SEV-1 + a safe leniency subset now; defer the riskier
natural-language pieces to the voice-engine feature (design with the recognizer).

| # | Severity | Feature | Disposition | Description |
|---|---|---|---|---|
| 11 | SEV-1 | F2/F3 | **Fixed** | Stuck-in-Paused: only exact "continue presentation" resumed → deck frozen. Added resume synonyms ("resume"/"continue"/±"the presentation") |
| 12 | SEV-2 | F2/F3 | **Fixed** | Natural filler/politeness dropped ("okay next slide", "next slide please") + "please" asymmetry. Added safe leading/trailing filler strip, re-audited |
| 13 | SEV-3 | F2/F3 | Deferred | Directional aliases ("go to the next slide", "move to…") → voice-engine feature |
| 14 | SEV-3 | F4 | Deferred | Natural numbers (ordinals, "one oh five", "last") + digit-concat "five and six"→56 → voice-engine |
| 15 | SEV-3 | F2/F3 | Deferred | Unicode/typographic punctuation ("…", curly quotes) not stripped → voice-engine |
| 16 | SEV-3 | F2/F3, F4 | Deferred | Out-of-range numbers emitted (0/neg/huge); **F7 MUST clamp** before ship (F4 caller contract) |

---

## Overall Notes

The security-critical property held under adversarial attack: the malicious-user agent **could
not force a single false command or a crash** — audience/ambient speech will not move the deck,
and there is no crash/hang/DoS. Every finding was the opposite (the grammar too strict → natural
commands do nothing), all fail-safe (a missed command, never a wrong jump).

Per triage (Option A): the **SEV-1 stuck-in-Paused** and the **SEV-2 natural-phrasing** gaps were
fixed test-first (resume synonyms + a small, re-audited filler-tolerance set that keeps audience
sentences as safe no-commands). The SEV-3 items (directional aliases, spoken-number naturalness,
Unicode punctuation, out-of-range clamping) are deferred to the **voice-engine feature**, where
command normalization is designed together with the grammar-constrained recognizer, and to **F7**
(range-clamp requirement). No open SEV-1/2 remain.

**Human probe (scenarios 5–9):** offered to Karl (`command_probe`); optional for this no-audio
logic feature — the 3 agent-tester submissions are the substantive results. Real *spoken*
testing (via microphone) happens in the voice-engine feature's UAT.
