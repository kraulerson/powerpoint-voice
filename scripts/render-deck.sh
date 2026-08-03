#!/usr/bin/env bash
# Render every slide of a .pptx to PNGs for a fidelity check (UAT / rehearsal).
# The deck stays entirely local — nothing is uploaded or committed.
#
#   scripts/render-deck.sh <deck.pptx> [out-dir]
#
# Default out-dir: ./render-out (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/.."

DECK="${1:?usage: scripts/render-deck.sh <deck.pptx> [out-dir]}"
OUT="${2:-render-out}"

if command -v brew >/dev/null 2>&1; then
  export PATH="$(brew --prefix llvm)/bin:$PATH"
  export CMAKE_PREFIX_PATH="$(brew --prefix qt)"
  export PKG_CONFIG_PATH="$(brew --prefix libzip)/lib/pkgconfig:$(brew --prefix pugixml)/lib/pkgconfig"
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build --target render_preview
./build/render_preview "$DECK" "$OUT"
echo "Open the PNGs in: $OUT"
