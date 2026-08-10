# Legal Review — Phase 3.9

**Date:** 2026-08-10
**Commit:** 63fa398
**Scope:** licence compliance for the binary that ships, and the project's own licensing.

---

## Finding 1 — the binary shipped with NO licence notices at all (RESOLVED)

Every one of the seven components requires that its notice accompany a **binary**
distribution. **None of it was shipped.** The bundle handed to the presenter on the morning of
2026-08-10 contained no licence text of any kind, and neither did the repository.

**Qt is the binding one.** Qt 6.11.1 is **LGPL-3.0-only** and is linked **dynamically** — which
is the arrangement LGPL-3.0 §4 exists for, and it carries three obligations:

| Obligation | Now met by |
|---|---|
| Prominent notice that Qt is used and is LGPL-covered | `THIRD_PARTY_NOTICES.md` |
| A copy of the GNU LGPL **and** the GNU GPL | `third_party/licenses/LGPL-3.0.txt`, `GPL-3.0.txt` |
| The user can relink against a modified Qt | Qt is dynamic in `Contents/Frameworks/`; replacing a framework is a file copy, then re-sign |

The other six carry attribution obligations of the same kind (Apache-2.0 §4 for Vosk and the
model; the BSD-3-Clause notice for libzip; the MIT notice for pugixml, miniaudio and doctest).

**Remediation, verified:** full texts in `third_party/licenses/`, `THIRD_PARTY_NOTICES.md`
written, and `scripts/make-test-build.sh` now stages them into
`Contents/Resources/licenses/` and **exits non-zero if any is missing**. Staged before
`codesign`, because adding to Resources afterwards invalidates the seal — which the first
version of the step did, and which `codesign --verify --deep --strict` caught.

## Finding 2 — the project had no licence (RESOLVED)

The repository is **public** and carried no `LICENSE` file, so no rights were granted to
anyone reading it; all rights reserved by default. That is a decision, not a defect, and it was
put to the Orchestrator rather than resolved silently.

**Karl Raulerson chose MIT, 2026-08-10.** `LICENSE` added. MIT is compatible with dynamically
linking Qt's LGPL-3.0-only code, which was the binding constraint on the choice.

## Licence inventory

| Component | Version | Licence | Obligation | Met |
|---|---|---|---|---|
| Qt | 6.11.1 | LGPL-3.0-only | Notice + LGPL & GPL texts + relinkable | Yes |
| Vosk | 0.3.44 | Apache-2.0 | Licence copy + attribution | Yes |
| vosk-model-small-en-us | 0.15 | Apache-2.0 | Licence copy + attribution | Yes |
| libzip | 1.11.4 | BSD-3-Clause | Copyright notice in binary distributions | Yes |
| pugixml | 1.16 | MIT | Copyright notice | Yes |
| miniaudio | 0.11.25 | MIT-0 **or** public domain | Notice (either alternative) | Yes |
| doctest | 2.4.11 | MIT | Copyright notice | Yes — build-time only, not shipped |

No copyleft licence obliges this project to release its own source: Qt's LGPL applies to Qt,
and the combined work may be MIT provided the §4 obligations above hold — which is what the
build step now enforces on every build.

## Data protection

- No personal data is collected, stored or transmitted. No accounts, no telemetry, no network.
- Speech is recognised on-device and never written to disk; recognised text passes from the
  decoder into the command gate and nowhere else (TM-011/012).
- The deck is read-only and never copied or uploaded.
- The macOS microphone consent string states exactly this, because that string is what the
  user is actually shown.

There is no GDPR/CCPA processing activity to document, because there is no processing.

## Not covered

- **No lawyer reviewed this.** It is an engineer reading licence texts. For a personal tool
  presented internally that is proportionate; for redistribution it is not.
- **Trademark** — "PowerPoint" is a Microsoft trademark. This project is not affiliated with or
  endorsed by Microsoft, and the name `powerpoint-voice` is descriptive use. Worth a real
  opinion before any public release; recorded rather than resolved.
- **Patents** — Apache-2.0 (Vosk) grants patent rights; MIT and BSD do not address patents. No
  patent search was performed.
