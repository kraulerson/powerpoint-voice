# WALK-STATE — powerpoint-voice full-rigor walk (resume file)

**Last updated:** 2026-08-05 (session 4; PRs #18-#24 all merged; UAT-4 agent arm running)
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

## 2b. DEPLOYMENT FACT — the showtime machine is NOT the dev machine (Karl, 2026-08-05)

**The talk runs on Karl's MacBook Pro M3 Max. Development and all agent testing happen on a Mac
mini, which has NO MICROPHONE.** Karl's instruction, verbatim: *"If it relies on talking to specific
hardware instead of a hardware api, it may fail on the macbook pro."*

Binding consequences for the voice work:
- **Bind to the API, never to a device.** Use CoreAudio's DEFAULT input device through miniaudio;
  never a device index, name, or enumeration order. Verified: miniaudio resolves AudioUnit/
  AudioComponent symbols at runtime — an OS API, not hardware.
- **Never assume the capture format.** The recogniser needs 16 kHz mono; a MacBook Pro's built-in
  microphone is a three-element ARRAY that CoreAudio typically presents at 48 kHz, and a headset or
  AirPods can be 44.1 kHz and/or stereo. Ask the device what it is and convert
  (`src/audio/audio_format.*`, GROUP AF tests). Assuming 16 kHz does not fail loudly on the wrong
  machine — it feeds the recogniser audio at 3x speed, which decodes as confident nonsense.
- **A multi-channel device is MIXED, never sampled on channel 0** — the array elements are not
  equivalent.
- **`NSMicrophoneUsageDescription` is mandatory.** macOS TERMINATES the process when it opens an
  audio input device without one; CMake's default generated Info.plist has no such key. Now supplied
  by `cmake/MacOSXBundleInfo.plist.in`. This would have crashed the app on the MacBook Pro and
  nowhere else — the Mac mini has no microphone to trigger it.
- Both machines are arm64 (dev M4 Pro, showtime M3 Max) and `libvosk.dyld` is universal2 with an
  arm64 slice, so the architecture is not a risk. The MICROPHONE is.
- **Voice cannot be verified on the dev box at all.** Karl's MacBook Pro is the only instrument.
- **Both machines run macOS 26 (Tahoe)** — confirmed by Karl 2026-08-05. That matters because Homebrew
  builds against the machine's own OS, so `libzip`/`pugixml` carry a **minos of 26.0** and set the
  bundle's floor (Qt itself only needs 14.0). A test build from this Mac mini therefore runs on his
  MacBook Pro as-is; it would NOT run on anything older.
- **Test builds:** `bash scripts/make-test-build.sh [outdir]` produces a self-contained, ad-hoc-signed
  `.app` (~33 MB zipped) and FAILS LOUDLY if anything still references `/opt/homebrew`. First launch
  needs right-click -> Open. This is not the release build — that is Phase 4 (ISSUE-003/010).

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

0. **Sync:** `git checkout main && git pull`; delete merged branches; `bash scripts/check-versions.sh`.
   Current: **main @ cd11cf8, 224 tests green, no open SEV-1.**
1. **UAT SESSION 4 IS OPEN** (`--start-uat 4` done, `agents_dispatched` marked). The agent arm is a
   5-tester + 5-skeptic workflow. **Per ISSUE-022 do NOT mark `gate_passed` on the agent arm alone** —
   every SEV-1 that reached a merged PR in this walk came from Karl, not the agents. Karl has asked
   NOT to be handed a build until voice works, so the plan agreed: run the agent arm now, hold the
   gate open, and fold his results in when F8c gives him something worth testing.
2. **F8c — the Vosk recogniser.** The last piece before voice actually works. Everything around it is
   already built and audited: `IRecognizer` + `RecognizerController` (the Active/Paused gate),
   `matchCommand` (the closed five-command grammar), `PresentationController` (the single slide-index
   funnel), F8a (format conversion), F8b (capture). **F8c is the join.**
   - use `vosk_recognizer_new_grm` — VERIFIED exported on both arm64 and x86_64; `set_grm` is NOT
     exported and must not be used
   - the model at `build/vosk/model/vosk-model-small-en-us-0.15` carries `Gr.fst`+`HCLr.fst`, so the
     grammar really constrains. A model with only `HCLG.fst` would degrade SILENTLY to a ~200k-word
     decoder — the audience-triggers-your-slides failure. Assert the layout at load.
   - invalid grammar JSON SEGFAULTS vosk; build the JSON, never interpolate
   - `docs/design-notes/voice-engine-design.md` holds the design AND three critics' NEEDS_CHANGES
     findings, which must be folded in first
   - link against `build/vosk/libvosk.dylib` (the PREPARED copy — the vendored one cannot load)
3. **F6 keyboard parity** · 4. **F5 transcript overlay + listening glyph + pre-show check**
5. **F7c render hardening** — BUG-21/22/23/29/33/34/42/44/54/55, and **BUG-40 (SEV-2, flaky
   worker-thread tests) which has now blocked or failed three commits.**
6. **Phase 2 exit -> Phase 3**, then Phase 4 (release.yml still invalid for C++ — ISSUE-003/010).

### The two open risks to the talk
- **The projector path has never been verified on real hardware** (section 2c). Highest talk-risk
  reduction available. Must be in the one build handed over after voice lands.
- **Voice cannot be verified here at all** — no microphone. Karl's MacBook Pro is the only instrument.

### Rules earned this session — keep applying
- **Do not record a root cause you have not reproduced or read from an instrument** (OBS-023). Look
  for the artefact FIRST: a macOS crash writes a full backtrace to `~/Library/Logs/DiagnosticReports/`.
- **A regression test that passes with the fix reverted is a finding**, not an inconvenience. It has
  caught four worthless tests this session. Mutate every new test before trusting it.
- **Every element of an evidence list must trace to output already read** (OBS-024). A check still
  running is written as "still running", never as its expected value.
- **`fix:` commits bypass the Build Loop and its audit** (ISSUE-025). Review every change, not just
  features — two rounds of adversarial review found TEN defects in my own work.
- **Every build handed to Karl carries a specific question it exists to answer** (section 2c).
- Use a `/tmp` backup for scratch edits, never `git checkout --` on a file with unstaged real work.

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
