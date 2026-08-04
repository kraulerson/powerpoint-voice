#!/usr/bin/env bash
# Convert a .pptx's EMF/WDP images (which no cross-platform renderer can decode)
# to PNG and repackage into a NEW .pptx. Runs entirely locally; the input deck is
# not modified and nothing is uploaded.
#
#   scripts/convert-deck-emf.sh <input.pptx> [output.pptx]
#
# Default output: "<input>-png.pptx" next to the input.
set -euo pipefail

IN="${1:?usage: scripts/convert-deck-emf.sh <input.pptx> [output.pptx]}"
OUT="${2:-${IN%.pptx}-png.pptx}"
SOFFICE="/Applications/LibreOffice.app/Contents/MacOS/soffice"

[ -f "$IN" ] || { echo "input not found: $IN" >&2; exit 1; }
# Absolutize so paths survive the `cd` into the work dir below.
IN="$(cd "$(dirname "$IN")" && pwd)/$(basename "$IN")"
OUT_DIR="$(cd "$(dirname "$OUT")" && pwd)"
OUT="$OUT_DIR/$(basename "$OUT")"
[ -x "$SOFFICE" ] || { echo "LibreOffice not found at $SOFFICE (brew install --cask libreoffice)" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
DECK="$WORK/deck"
mkdir -p "$DECK"
( cd "$DECK" && unzip -q "$IN" )

MEDIA="$DECK/ppt/media"
# Collect EMF images (WDP/JPEG-XR is skipped: LibreOffice cannot import it and
# hangs trying). An isolated per-run profile avoids the soffice profile-lock
# deadlock, and ONE invocation converts them all (no per-file cold-start).
shopt -s nullglob
emfs=("$MEDIA"/*.emf)
converted=0
if [ ${#emfs[@]} -gt 0 ]; then
  PROFILE="$WORK/lo-profile"
  "$SOFFICE" --headless "-env:UserInstallation=file://$PROFILE" --convert-to \
    'png:draw_png_Export:{"PixelWidth":{"type":"long","value":1920},"PixelHeight":{"type":"long","value":1080}}' \
    --outdir "$MEDIA" "${emfs[@]}" >/dev/null 2>&1 || true
  for img in "${emfs[@]}"; do
    base="$(basename "${img%.*}")"
    if [ -f "$MEDIA/$base.png" ]; then
      rm -f "$img"
      converted=$((converted + 1))
      echo "  converted $(basename "$img") -> $base.png"
    else
      echo "  FAILED to convert $(basename "$img") (left as-is)" >&2
    fi
  done
fi
failed=$(ls "$MEDIA"/*.wdp 2>/dev/null | wc -l | tr -d ' ')

# Repoint relationship targets .emf -> .png (blips reference the rId, so the
# slide XML itself needs no change). WDP targets are left untouched.
find "$DECK" -name '*.rels' -print0 | while IFS= read -r -d '' rels; do
  sed -i '' -E 's/\.emf"/.png"/g' "$rels"
done

# Ensure [Content_Types].xml declares a PNG default.
CT="$DECK/[Content_Types].xml"
if [ -f "$CT" ] && ! grep -q 'Extension="png"' "$CT"; then
  sed -i '' 's#<Types \([^>]*\)>#<Types \1><Default Extension="png" ContentType="image/png"/>#' "$CT"
fi

# Repackage ([Content_Types].xml first, per OOXML convention).
rm -f "$OUT"
( cd "$DECK" && zip -q -X "$OUT" "[Content_Types].xml" && zip -q -rX "$OUT" . -x "[Content_Types].xml" )

echo "Converted $converted image(s), $failed failed. Wrote: $OUT"
