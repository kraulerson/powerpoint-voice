# ADR-0001: Application architecture — Qt 6 + Vosk on a from-scratch OOXML renderer

**Status:** Accepted
**Date:** 2026-08-03
**Decided by:** Karl Raulerson (Orchestrator), Phase 1 Step 1.2 decision gate
**Phase:** 1

## Context

powerpoint-voice is a fully-offline C++ desktop app (hard constraints: C++, zero runtime
network, no auth) that renders .pptx decks via a from-scratch renderer (Orchestrator
decision over the embedded-LibreOffice recommendation — intake §11 risk 1) and executes a
five-phrase voice grammar with strict keyboard parity, shipping rehearsal-ready by
2026-08-08 for a live executive presentation (~2026-08-10). macOS-first; Windows/Linux
portable + CI-built. The desktop Platform Module's default recommendations (Tauri/Electron)
are excluded outright by the C++ hard constraint. Data classification: confidential
(deck + room audio) — everything on-device.

## Options Evaluated

Three options were presented at the decision gate on 2026-08-03 (full side-by-side previews
recorded in the session; summarized here):

- **A — Qt 6 + Vosk (selected):** Qt 6.8 LTS, qtbase modules only (Core/Gui/Widgets;
  LGPLv3, dynamically linked); renderer = our OOXML slide model painted with
  QPainter/QTextLayout (text shaping, wrapping, embedded fonts via QRawFont, image codecs
  via QImage); speech = Vosk 0.3.45 (Apache-2.0, C API) with a **grammar-constrained**
  recognizer limited to the five phrases + number vocabulary; mic capture = miniaudio
  (MIT, single header — deliberately avoiding Qt Multimedia's licensing/bulk); display
  management = QScreen (hotplug, HiDPI); accessibility = Qt Accessibility bridge.
  Build: CMake ≥3.29 + Ninja; Qt in CI via aqtinstall; deps pinned via FetchContent
  (pugixml 1.15, libzip 1.11, miniaudio, Vosk prebuilt lib + model vosk-model-small-en-us-0.15
  bundled in the app package). Packaging: macdeployqt → DMG (macOS primary); windeployqt/
  linuxdeploy post-MVP. Binary ~50-80MB + ~40MB model.
- **B — SDL2 minimal stack:** SDL2 + FreeType + HarfBuzz + stb_image + miniaudio + Vosk.
  ~15MB, all-permissive licenses.
- **C — Qt 6 + platform-native speech:** As A, but macOS speech via Apple
  SFSpeechRecognizer on-device (ObjC++ bridge), Vosk elsewhere.

## Decision

**Option A.** One battle-tested framework (Qt) owns the exact surfaces where the
from-scratch renderer bet is riskiest — text shaping/layout, embedded font loading, image
decoding, HiDPI, multi-display hotplug — plus real accessibility APIs for the Phase 3
audit mandate (Frontend/UI and Accessibility are "Partially" in the Competency Matrix).
Vosk's closed grammar is the strongest available mitigation for the audience-false-trigger
risk that drove the Q1 two-word-grammar change: the recognizer cannot match anything
outside the command vocabulary. Everything runs on-device (ZDR posture preserved);
LGPLv3 (Qt, dynamically linked) is permitted by the framework's license deny policy
(strong copyleft only: GPL/AGPL/SSPL).

Structured logging (spdlog 1.14, MIT) with session-scoped correlation IDs from day 1;
log content constrained by Manifesto Q7/Q8 (no heard-text by default, no deck content
ever). Secrets management: N/A — no credentials exist in this application. Auth: N/A
(hard constraint). Auto-update: manual (GitHub Releases). Scalability vs velocity:
single-user local app — velocity wins everywhere scaling would cost complexity.

## Rejected Alternatives

- **B — SDL2 minimal stack:** hand-rolled text shaping/layout compounds the highest-risk
  component (renderer) with the hardest sub-problem (text), inside one week; no
  accessibility API at all — irreconcilable with the intake's WCAG-equivalent bar and the
  mandated Phase 3 accessibility tooling. Rejected for MVP; noted as a plausible
  post-MVP slimming target only if a11y strategy exists.
- **C — Qt + native speech:** loses Vosk's closed-grammar guarantee on macOS (Apple
  provides contextual hints, not a constrained grammar), widening the false-trigger
  surface the Q1 decision explicitly narrowed; adds a second speech backend + ObjC++
  bridge + per-macOS-version on-device verification inside the same week. Rejected for
  MVP; recorded as a v1.1 experiment if rehearsal shows Vosk-small accuracy is marginal.
- **Tauri / Electron (Platform Module defaults):** contradict the C++ hard constraint
  (intake §6.4). Not evaluated further.

## Consequences

- Qt 6.8 LTS becomes the project's dominant dependency; the team (Orchestrator "Partially"
  on UI) relies on Phase 3 mandated UI/a11y tooling plus Qt's maturity.
- LGPL compliance obligations: dynamic linking only, ship license notices, no static Qt.
  Verified again mechanically at the Phase 3 license gate.
- CI needs aqtinstall (Qt is not an apt one-liner); the project declares extra apt/CI
  deps via .github/ci-deps-apt.txt (mechanism already in ci.yml).
- The renderer remains from-scratch per the Orchestrator's standing decision — Qt reduces
  its substrate risk but does not change its scope fence (text+images tier, visible
  placeholders for unsupported elements).
- Speech accuracy ceiling is vosk-model-small-en-us; if rehearsal metrics (§2.3: ≥95%)
  miss, the recorded fallbacks are keyboard-primary presentation (exit criteria) and the
  Option C experiment post-MVP.
