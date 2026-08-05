# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-05 (session 3; PR #17 merged, PR #18 open — BUG-32 + the CORRECTED BUG-30)
**A fresh session (e.g. post-/compact) must be able to continue from THIS FILE ALONE.**
**To resume: read this file top-to-bottom, then `git -C <project> log --oneline -5` and
`bash scripts/process-checklist.sh --status` to confirm live state, then continue at "NEXT".**

---

## 1. What this is

Dogfood walk of the **Solo Orchestrator framework** at maximum rigor, building a REAL app:
**powerpoint-voice**, a voice-controlled offline presentation controller for Karl's live
executive presentation (~week of 2026-08-10). Dual purpose: ship the app AND stress-test the
framework's most rigorous path, logging every stumble.

**Roles & rules (do not soften):**
- Karl Raulerson plays EVERY human approver/reviewer/tester slot. The orchestrator (Claude,
  junior-dev persona) STOPS at every human decision — never self-attests, never simulates
  Karl's approval, presents evidence per item before asking.
- Framework bugs are FINDINGS (→ `WALK-ISSUE-LOG.md`), NEVER fix targets. Never edit any file
  inside the `../solo-orchestrator/` clone (one documented exception: the NEW file
  `templates/pipelines/ci/github/cpp.yml`).
- No enforcement bypasses ever (no `--no-verify`, `--admin`, `--force` past a gate). Escape
  hatches only where documented, every use logged.
- Never conclude from truncated output; never pipe `git commit` (verify with `git log -1`).

## 2. Fixed configuration

- Framework clone: `../solo-orchestrator` @ `6417a25` (HTTPS).
- **deployment=organizational · gov_mode=production (NO POC) · track=full · enforcement=strict**
- platform=desktop · language=cpp · host=github.
- Repo `kraulerson/powerpoint-voice` — **PUBLIC**, org-mode branch protection LIVE (main takes
  PRs only, 1 required review, enforce_admins on).
- **Architecture (ADR-0001, Karl-selected):** C++20, **Qt 6** (from-scratch OOXML renderer),
  **Vosk** on-device speech, libzip+pugixml parse, CMake+Ninja. Fully offline; no auth/network.

## 3. Standing protocols (LEARNED — keep applying)

1. **Merges to main:** agent prepares PR + green CI on BOTH platforms, then STOPS. Karl
   approves/merges via his `kraulerson-reviewer` account. Every merge → a row in
   `WALK-UNBLOCK-AUDIT.md` (resolves ISSUE-006: solo org-mode can't self-merge).
2. **Recorder identity (resolves ISSUE-008):** repo git identity is `kraulerson-reviewer`
   (set repo-locally). The recorder commits what approver **Karl Raulerson** decides. NEVER
   author an APPROVAL_LOG approver row as "Karl Raulerson" — the self-approval verifier blocks
   every solo arm. Approver ROWS name Karl; commit AUTHOR is kraulerson-reviewer.
3. **ZDR / Confidential deck (intake §5.1.1):** the real deck
   `../Solo Orchestrator - FirstService IT Summit.pptx` is Confidential, lives OUTSIDE the
   repo, is NEVER committed (public repo), and its CONTENT is never sent to the LLM. Render it
   locally for fidelity checks but **do NOT open/Read the rendered PNGs** — Karl is the eyes.
   Reading structure/metadata (slide counts, element types, attribute values, file sizes) is OK.
4. **Project CLAUDE.md is binding:** phase-entry commands own `current_phase`; docs-only commits
   bypass the Build Loop gate; `feat:` needs an active `--start-feature`; `.claude/test-command`
   → `scripts/run-tests.sh` runs the full suite at every source commit.
5. Product truth = `PROJECT_INTAKE.md` + `PRODUCT_MANIFESTO.md`. Command grammar is the five
   TWO-word phrases: **"next slide" / "previous slide" / "pause presentation" /
   "continue presentation" / "go to slide N"** (Karl's Q1 change; supersedes intake single words).

## 4. Current position (end of session 2, 2026-08-05)

- **Phase 2 (Construction), current_phase=2.** Phases 0 & 1 gate-approved by Karl.
- **THE PRODUCT PRESENTS.** This is the milestone of session 2. Run it:
  `./build/powerpoint_voice.app/Contents/MacOS/powerpoint_voice <deck.pptx>` — it opens the deck
  off-thread, pre-renders every slide off-thread, and shows it fullscreen with keyboard control.
- **Features complete (each through the full Build Loop, tests-first, security-audited):**
  F1a deck loader · F1b slide renderer · F4 number parser · F2/F3 command grammar + dispatch ·
  **F7a presentation funnel** · **F7b usable presenter**.
- **183 tests green** (macOS + Ubuntu CI), in TWO binaries: `pptv_tests` (core, QGuiApplication) and
  `pptv_ui_tests` (widgets, QApplication). ASan+UBSan clean; ThreadSanitizer clean; Semgrep 0.
- **UAT sessions 1, 2 and 3 all complete and ARCHIVED** to `docs/test-results/` (session 3 same-day).
  Counter reset — clear to start the next feature.
- **Karl's real deck is 10 slides** (~316 MB of rasters) — so BUG-22 (unbounded raster cache) is
  **not a risk for the 2026-08-10 talk**. He has NOT yet run the UAT-3 human scenarios
  (`tests/uat/sessions/2026-08-05-session-3/test-session-3-v1.md`) against it; that is still open
  and is the best remaining fidelity check.
- **PR #17 (UAT-3 remediation) is OPEN** at session end. On resume: `git checkout main && git pull`;
  if not merged it is on branch `walk/uat-3`.
- Voice is NOT wired yet. The deps (libvosk 0.3.44 + the 40MB model + miniaudio) are vendored via
  git-LFS, pinned + SHA-256 verified (`third_party/PROVENANCE.md`).

## 5. NEXT (in order) — resume here

0. **Sync:** `git checkout main && git pull`; delete merged branches; append a
   `WALK-UNBLOCK-AUDIT.md` row for the PR #18 merge. Run `bash scripts/check-versions.sh`.
1. **ASK KARL TO RE-TEST THE ORIGINAL .pptx** and confirm BUG-30 (crash) and BUG-31 (unquittable)
   are closed, and that backgrounds now appear (BUG-32). **BUG-30 is NOT self-verifiable** — it does
   not reproduce on this machine; his run is the only instrument. Also worth his eyes: slides
   1/2/8/10 have DARK backgrounds now, so any previously-invisible white text should appear.
2. **F7c — the deferred hardening** (`--start-feature "F7c-render-hardening"`). Closes:
   - **BUG-34** (new, SEV-2, the residual of BUG-30): the pre-render worker still uses Qt's font
     database off the GUI thread, so a SPONTANEOUS theme change (dark mode at sunset, a display
     attaching, RustDesk reconnecting) landing mid-pre-render can still race it. Fix: resolve every
     family the deck names to an INSTALLED family once on the GUI thread and hand the renderer
     concrete families, so the worker never enters font fallback. **This also fixes BUG-33** (slow
     first render: ~50 families, almost none on macOS, each a full fallback search on first use).
   - **BUG-21**: the TM-018 caps count shapes + text runs only. Add total CHARACTERS and total
     DECLARED image pixels, completing the ratified four-cap set (TM-018.3-A).
   - **BUG-22**: the always-on **2 GB** raster window (ratified Bible section 3 A3-1(3) / B1-A).
   - **BUG-23**: isPlaceholder discarded; `HoldLastGood` unused; a `const` `std::move`; two
     parentless widgets.
   - **BUG-29**: the 19 SEV-3/4 UAT-3 findings — full detail in
     `tests/uat/sessions/2026-08-05-session-3/submissions/uat-3-triage.md`. Highest-value: notices
     never expire; Ctrl+Shift+F/R are consumed then dropped; typed slide numbers give no feedback;
     the blackout hint is shown to the AUDIENCE and is factually wrong; **8 ctest entries execute
     ZERO test cases**; libpng writes deck-derived bytes to stderr (TM-012/013).
3. **F6 keyboard parity** — formalise the five commands + keybinding config on the shared
   matchCommand -> PresentationController path.
4. **UAT session 4** (fires after 2 more features). **Per ISSUE-022, do NOT mark `gate_passed`
   until KARL's human results are in** — the checklist does not enforce this and every SEV-1 that
   has reached a merged PR in this walk came from his arm, not the agents'.
5. **Voice engine** — `docs/design-notes/voice-engine-design.md` holds the design AND the three
   critics' NEEDS_CHANGES findings, which must be folded in first. Highest-value:
   `vosk_recognizer_set_grm` is NOT exported by the 0.3.44 macOS lib (green on Ubuntu CI, fails on
   the showtime Mac); a model without the expected layout silently degrades to a full ~200k-word
   decoder; invalid grammar JSON SEGFAULTs; OOV grammar tokens are silently dropped; `assert()` is a
   no-op in this forced-Release build. **Karl tests over RustDesk with NO microphone — voice work
   needs the physical machine.**
6. **F5 transcript overlay** + listening glyph + pre-show check (final MVP item).
7. **Phase 2 exit -> Phase 3** (no open SEV-1/2, all MVP features, CI green), then the five-scanner
   gate, six-reviewer eval, Karl's auditor sign-offs, dual 3->4 approval.
8. **Phase 4** — author the C++ macOS build/sign steps in `release.yml` (ISSUE-003/010), rollback
   test, go-live smoke test, HANDOFF.md, tag **v1.0.0**. Walk DONE.

### Standing rule earned this session (OBSERVATION-023)
**Do not record a root cause you have not reproduced or read from an instrument.** Say "hypothesis"
until then, and say so to Karl too. Look for the artefact FIRST — a macOS crash writes a full
backtrace to `~/Library/Logs/DiagnosticReports/`, and reading it took two minutes after a full cycle
of theorising. **A regression test that passes with the fix reverted is a finding, not an
inconvenience** — it is the cheapest disproof of a diagnosis available, and it is what caught this.

## 6. Build / run recipe (macOS local — REQUIRED env)

```
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
(cd build && ctest --output-on-failure)   # 66 tests
```
- Full test command (what the commit gate runs): `bash scripts/run-tests.sh`.
- Render a deck to PNGs (fidelity check): `bash scripts/render-deck.sh <deck.pptx>` → `render-out/`.
- Convert a deck's EMF images to PNG: `bash scripts/convert-deck-emf.sh <in.pptx> <out.pptx>`.
- CI ubuntu deps: `.github/ci-deps-apt.txt` (qt6-base-dev, libgl1-mesa-dev, pkg-config,
  libzip-dev, libpugixml-dev, fonts-dejavu-core).

## 7. Toolchain installed locally

cmake 4.4.2, ninja, llvm/clang-format/clang-tidy 22, Qt 6.11.1 (brew), libzip 1.11.4,
pugixml 1.16, pkgconf 3.0.5, LibreOffice (soffice, for EMF conversion), gh (as kraulerson +
kraulerson-reviewer), semgrep/gitleaks/snyk/docker.

## 8. Companion files (all committed)

- `WALK-ISSUE-LOG.md` — append-only findings (ISSUE-001…012 + OBSERVATION-005/009 + S-1…S-21
  smooth notes). Framework findings incl. ISSUE-002 (generate_ci ships other.yml for a
  discovered language), ISSUE-006/008 (solo org-mode blockers, resolved), ISSUE-007 (gate
  deadlock), ISSUE-010 (invalid release.yml), ISSUE-011 (C++ lockfile gap).
- `WALK-UNBLOCK-AUDIT.md` — every Karl-executed merge/un-block (rows 1–8 so far).
- `BUGS.md` — BUG-1…10 (all Fixed or Won't-Fix-MVP). No open SEV-1/2.
- `APPROVAL_LOG.md` — 6 org pre-conditions + Phase 0→1 (Sponsor) + Phase 1→2 (STA) approvals.
- Memory pointer: `~/.claude/projects/-Users-karl-Documents-Claude-Projects-powerpoint-voice/memory/powerpoint-voice-walk.md`.

## 9. Findings tally (for WALK-REPORT.md at the end)

**See the `FINDINGS INDEX & CLASSIFICATION` section at the END of `WALK-ISSUE-LOG.md`** — it splits
every entry into **A. FRAMEWORK** (the only candidate fixes for solo-orchestrator), **B. PROJECT**
(ours), and **C. smooth notes**, and is the source for WALK-REPORT.md. Keep appending there; the log
is append-only, so corrections go in as new entries rather than edits.

**21 numbered findings** — ~16 FRAMEWORK (2 Blocker, 8 Major, 3 Moderate, 4 Minor) + PROJECT ones —
plus ~23 smooth notes. Newest and highest-value:
- **ISSUE-017** (Major): the Build Loop never asks whether the PRODUCT is demonstrable end-to-end.
  Four features shipped at full rigor while the app was still a dark window.
- **ISSUE-018** (Major): UAT remediation gets no re-audit — the BUG-11 fix introduced the SEV-2
  BUG-17 regression and passed the gate, CI and a merge.
- **ISSUE-019** (Major): the Build Loop cannot express a STAGED feature without either a false
  attestation or a ten-stage mega-commit. Resolved by splitting F7 into sub-features.
- **ISSUE-020** (Moderate): the enforced 9-step UAT checklist omits the archive step the same
  document mandates — two sessions passed `gate_passed` with nothing archived. Found because KARL
  asked. Now archived, and session 3 was archived same-day.
- **OBSERVATION-016** (Moderate): clang-tidy has never run — CI guards it on a `.clang-tidy` that
  has never existed, so a documented control reported green while doing nothing.
- **OBSERVATION-021** (PROJECT, mine): three times "fixed" meant *edited and the suite still passes*
  rather than *verified*. New rule: re-run the instrument that FOUND the defect.

**The recurring shape across ISSUE-016/017/019/020: the enforced control and the documented
procedure disagree, and enforcement is what gets followed.** That is the single most useful thing
this walk has produced for the framework.

**The framework's strongest evidence FOR itself** (belongs in the report beside the defects): the
per-feature security audit has caught real ship-blocking bugs in EVERY feature — F1a 3 Critical +
1 High; F1b 1 Critical + 2 High; F4 overflow/wrong-jump; F2/F3 an audio-thread `std::terminate`;
F7a 2 HIGH gate-bypasses; F7b **5 Criticals** including a use-after-free that SEGV'd on the first
key press. UAT has caught what audits missed: UAT-1 seven real-deck bugs (2 SEV-1, invisible text);
UAT-3 the privacy blackout un-blanking itself and a double-letterbox that would have shown 75% of
the deck on the projector for the whole talk. Neither control is redundant.
