#!/usr/bin/env bash
# Build a SELF-CONTAINED macOS .app that runs on a machine without Homebrew.
#
# This is a TEST build, not a release: ad-hoc signed, not Developer-ID signed and
# not notarized, so the first launch needs right-click -> Open. The real signed
# release build is Phase 4 work (see ISSUE-003/010 — the generated release.yml is
# not usable for C++ yet).
#
# Why this script exists: the steps below were worked out by hand once, and three
# of them are the kind that get forgotten and produce a bundle that runs on the
# build machine and nowhere else.
set -euo pipefail

OUT="${1:-$HOME/Desktop}"
BUILD="${TMPDIR:-/tmp}/pptv-dist"
APP="$BUILD/powerpoint_voice.app"

export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"

echo "==> configure + build"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target powerpoint_voice

echo "==> embed Qt and the non-system dylibs"
"$(brew --prefix qt)/bin/macdeployqt" "$APP" -always-overwrite || true
# macdeployqt copies transitive dependencies but does NOT always rewrite their own
# LC_ID_DYLIB, so a copied library can still announce a Homebrew path and be loaded
# from Homebrew on a machine that happens to have it — or fail on one that does not.
for lib in "$APP"/Contents/Frameworks/*.dylib; do
  [ -e "$lib" ] || continue
  install_name_tool -id "@rpath/$(basename "$lib")" "$lib" 2>/dev/null || true
done

# LICENCE COMPLIANCE. Every one of the seven components requires that its notice
# accompany a BINARY distribution, and none of it shipped until 2026-08-10 — found
# during the Phase 3 legal review. Qt is the binding one: LGPL-3.0-only, dynamically
# linked, which obliges us to ship a copy of the LGPL and the GPL alongside the
# combined work. The build FAILS below if any of it is missing, because a compliance
# step that can be silently skipped is one that eventually is.
#
# BEFORE the signature, deliberately: adding anything to Contents/Resources AFTER
# codesign invalidates the seal, and `codesign --verify --deep --strict` then fails
# with "a sealed resource is missing or invalid". Measured, not guessed — that is
# exactly what the first version of this step did.
echo "==> stage third-party licences"
mkdir -p "$APP/Contents/Resources/licenses"
cp THIRD_PARTY_NOTICES.md "$APP/Contents/Resources/licenses/"
cp third_party/licenses/*.txt "$APP/Contents/Resources/licenses/"
for required in LGPL-3.0.txt GPL-3.0.txt Apache-2.0.txt libzip-BSD-3-Clause.txt \
                pugixml-MIT.txt miniaudio-MIT-0-or-public-domain.txt \
                THIRD_PARTY_NOTICES.md; do
  test -s "$APP/Contents/Resources/licenses/$required" || {
    echo "FAILED: licence text '$required' is missing from the bundle." >&2
    echo "        Distributing without it is a licence violation, not a cosmetic gap." >&2
    exit 1
  }
done

echo "==> re-sign (install_name_tool invalidates every signature it touches)"
find "$APP" -name "*.dylib" -print0 | xargs -0 -I{} codesign -f -s - {} 2>/dev/null || true
codesign -f -s - --deep "$APP"

echo "==> verify the speech engine and model are INSIDE the bundle"
# Without these the app launches and then reports "model missing" — on the
# presenter's machine only, because the build tree exists here and nowhere else.
test -f "$APP/Contents/Frameworks/libvosk.dylib" || { echo "FAILED: libvosk not staged" >&2; exit 1; }
test -f "$APP/Contents/Resources/vosk-model/graph/Gr.fst" || { echo "FAILED: model not staged, or not grammar-capable" >&2; exit 1; }
test -f "$APP/Contents/Resources/vosk-model/graph/HCLr.fst" || { echo "FAILED: model lacks a dynamic graph; grammar would be IGNORED" >&2; exit 1; }

echo "==> verify self-containment"
leaks=$(find "$APP" -type f \( -perm -u+x -o -name "*.dylib" \) \
        -exec sh -c 'otool -L "$1" 2>/dev/null | tail -n +2 | grep -q /opt/homebrew && basename "$1"' _ {} \; | sort -u)
if [ -n "$leaks" ]; then
  echo "FAILED: these still reference /opt/homebrew and will not run elsewhere:" >&2
  echo "$leaks" >&2
  exit 1
fi
codesign --verify --deep --strict "$APP"

# The binding constraint is whichever component demands the newest macOS. Homebrew
# builds against the machine's own OS, so libzip/pugixml typically demand THIS
# machine's version — which is what decides whether the build runs on the target.
minos=$(find "$APP" -type f \( -perm -u+x -o -name "*.dylib" \) \
        -exec sh -c 'otool -l "$1" 2>/dev/null | grep -A4 LC_BUILD_VERSION | grep minos | awk "{print \$2}"' _ {} \; \
        | sort -V | tail -1)

echo "==> package"
(cd "$BUILD" && ditto -c -k --sequesterRsrc --keepParent powerpoint_voice.app "$OUT/powerpoint-voice-test-build.zip")

echo
echo "Wrote $OUT/powerpoint-voice-test-build.zip"
echo "  arch:            $(lipo -info "$APP/Contents/MacOS/powerpoint_voice" | sed 's/.*: //')"
echo "  requires macOS:  $minos or newer"
echo "  first launch:    right-click -> Open (ad-hoc signed, not notarized)"
