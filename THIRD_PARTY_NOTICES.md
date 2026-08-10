# Third-Party Notices

`powerpoint-voice` is built from the components below. Every one of them requires that its
notice accompany a **binary** distribution, and until 2026-08-10 none of it was shipped —
found during the Phase 3 legal review. Full licence texts are in `third_party/licenses/`
and are copied into the application bundle at `Contents/Resources/licenses/`.

Versions and SHA-256 digests are pinned in `third_party/PROVENANCE.md` and `sbom.json`.

---

## Qt 6.11.1 — LGPL-3.0-only

Qt is used **unmodified** and linked **dynamically**. The Qt frameworks inside the
application bundle are the upstream binaries, relocated by `macdeployqt` and re-signed; no Qt
source was changed.

This is the arrangement LGPL-3.0 §4 is written for, and it carries three obligations, all met
here:

1. **Prominent notice that Qt is used and is covered by the LGPL** — this section.
2. **A copy of the GNU LGPL and the GNU GPL** — `third_party/licenses/LGPL-3.0.txt` and
   `third_party/licenses/GPL-3.0.txt` (LGPL-3.0 incorporates the GPL-3.0 by reference).
3. **The user must be able to relink against a modified Qt.** Qt is dynamically linked in
   `Contents/Frameworks/`, so replacing a framework with a compatible build is a file copy.
   Note that the bundle is code-signed: after replacing a framework, re-sign with
   `codesign -f -s - powerpoint_voice.app`, which the build script does anyway.

Qt sources: <https://download.qt.io/official_releases/qt/6.11/6.11.1/single/>

## Vosk 0.3.44 — Apache-2.0

Offline speech recognition. Used unmodified as a dynamic library
(`Contents/Frameworks/libvosk.dylib`); its install name is rewritten to `@rpath` and it is
re-signed, which is a relocation, not a modification of the code.

Licence: `third_party/licenses/Apache-2.0.txt`. Upstream:
<https://github.com/alphacep/vosk-api>

## vosk-model-small-en-us 0.15 — Apache-2.0

The speech model, extracted into `Contents/Resources/vosk-model/` at build time. Unmodified.

Licence: `third_party/licenses/Apache-2.0.txt`. Upstream:
<https://alphacephei.com/vosk/models>

## libzip 1.11.4 — BSD-3-Clause

OOXML package reading. Linked from the system/Homebrew build; unmodified.

Licence and copyright notice: `third_party/licenses/libzip-BSD-3-Clause.txt`

## pugixml 1.16 — MIT

XML parsing. Unmodified.

Licence and copyright notice: `third_party/licenses/pugixml-MIT.txt`

## miniaudio 0.11.25 — public domain (Unlicense) **or** MIT-0, at your option

Audio capture. Single header, committed verbatim and unmodified.

Both licence alternatives: `third_party/licenses/miniaudio-MIT-0-or-public-domain.txt`

## doctest 2.4.11 — MIT

Test framework. **Build-time only — not present in the shipped application.** Listed for
completeness of the SBOM.

Licence and copyright notice: `third_party/licenses/doctest-MIT.txt`

---

## What this project is licensed under

**MIT** — see `LICENSE`. Chosen by Karl Raulerson, 2026-08-10, during the Phase 3 legal review,
which is what surfaced the fact that a public repository carried no licence at all (and so
granted nothing to anyone reading it).

MIT is compatible with dynamically linking Qt's LGPL-3.0-only code, which was the binding
constraint on the choice: the combined work may be distributed under MIT provided the LGPL
obligations above continue to be met — which is what the `make-test-build.sh` licence-staging
step now enforces on every build.

## Compliance status

| Obligation | Status |
|---|---|
| Licence texts present in the repository | Met — `third_party/licenses/` |
| Licence texts shipped inside the app bundle | Met — `Contents/Resources/licenses/`, verified by `scripts/make-test-build.sh` |
| Qt dynamically linked and relinkable | Met |
| Attribution notices preserved | Met — this file |
| Project's own licence declared | Met — MIT (`LICENSE`), Karl Raulerson 2026-08-10 |
