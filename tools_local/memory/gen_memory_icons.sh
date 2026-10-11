#!/bin/bash
# Regenerate MEMORY's card faces and shelf mark from the vendored Lucide pin.
#
#   brew install librsvg          # rsvg-convert
#   ./tools_local/memory/gen_memory_icons.sh
#
# The output is committed, because regenerating needs librsvg and a checkout
# should build without it.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
GEN=freeink-sdk/libs/assets/Icons/tools/gen_icons.py
SVG=freeink-sdk/libs/assets/Icons/lucide/icons
python3 "$GEN" --manifest tools_local/memory/faces.txt --svgdir "$SVG" --sizes 48 \
  --out src/apps_local/memory/MemoryFaces.h
python3 "$GEN" --manifest tools_local/memory/icons.txt --svgdir "$SVG" --sizes 32 \
  --out src/apps_local/memory/MemoryIcons.h
echo "wrote src/apps_local/memory/MemoryFaces.h and MemoryIcons.h"
