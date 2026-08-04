# Third-Party Vendored Dependencies — Provenance & Integrity

Vendored (committed) so the app builds and runs **fully offline from a bare clone** — no
runtime download, no build-time fetch (Orchestrator decision, 2026-08-04). Binaries are stored
via **git-LFS** (see `.gitattributes`). Every artifact below is pinned by exact version + SHA-256.

## Vosk (offline speech recognition) — Apache-2.0

**Version pinned: 0.3.44.** ADR-0001 named 0.3.45, but **0.3.45 ships no macOS build** (Linux +
Windows only); 0.3.44 is the latest version with a macOS `universal2` binary (arm64 + x86_64),
required for the Apple-Silicon showtime machine. Recorded as a walk deviation; ADR/Bible updated.

| Artifact | Source | Source SHA-256 (verified) | Committed file SHA-256 |
|---|---|---|---|
| macOS lib | PyPI `vosk-0.3.44-py3-none-macosx_10_6_universal2.whl` | `029d0b3d…82801` (PyPI digest) | `libvosk.dyld` = `82fdfba0dde392a7dbba70f9c1a17f6e3da27a50444a9f53939508525a1e6fdf` |
| Linux lib | PyPI `vosk-0.3.44-py3-none-manylinux_2_12_x86_64…whl` | `2652ef21…bf76` (PyPI digest) | `libvosk.so` = `0f6e82ed2757ca422975d22f811dd79b577932a97711eafa1d6e89d79d46d4a4` |
| Header | `alphacep/vosk-api` tag **v0.3.45** `src/vosk_api.h` | — | `vosk_api.h` = `4c96a346cb233c0dd2f9e84aa0d1e54bad05d738e14545f0f0227a1ee94f4261` |
| Model | `alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip` | — | `vosk-model-small-en-us-0.15.zip` = `30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498` |

- **Wheel → lib chain:** each PyPI wheel was downloaded and verified against PyPI's published
  SHA-256 (both `OK`) before extracting `vosk/libvosk.{dyld,so}`.
- **Header:** no `v0.3.44` git tag exists (0.3.44 was a PyPI-only wheel build), so the header is
  taken from the stable `v0.3.45` tag. **ABI-verified:** every symbol the app uses
  (`vosk_model_new`, `vosk_recognizer_new_grm`, `vosk_recognizer_accept_waveform`,
  `vosk_recognizer_final_result`) resolves in the 0.3.44 `libvosk.dyld` (`nm -gU`).
- **macOS arch:** `lipo -archs libvosk.dyld` → `x86_64 arm64` (universal2; runs on Apple Silicon).
- **Model:** the small en-US model (~40MB zip). Committed as the ZIP; CMake extracts it at build
  and the build verifies the SHA-256 above before extracting.

## miniaudio (audio capture) — MIT / public-domain (dual)

| Artifact | Source | SHA-256 |
|---|---|---|
| `miniaudio.h` | `mackron/miniaudio` tag **0.11.25** | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` |

Single-header, committed as plain text (source, diffable).

## Re-verify

```
shasum -a 256 third_party/vosk/lib/macos-universal2/libvosk.dyld \
  third_party/vosk/lib/linux-x86_64/libvosk.so \
  third_party/vosk/include/vosk_api.h third_party/miniaudio/miniaudio.h \
  third_party/vosk/model/vosk-model-small-en-us-0.15.zip
```
