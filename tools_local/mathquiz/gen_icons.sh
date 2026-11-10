#!/bin/bash
# Regenerate src/apps_local/mathquiz/MathQuizIcons.h from tools_local/mathquiz/icons.txt.
#
#   brew install librsvg          # rsvg-convert, the only external dependency
#   ./tools_local/mathquiz/gen_icons.sh
#
# The output is committed, because regenerating needs librsvg and a checkout
# should build without it. Edit icons.txt, run this, commit both.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
uv run --quiet --with pillow python freeink-sdk/libs/assets/Icons/tools/gen_icons.py \
  --manifest tools_local/mathquiz/icons.txt \
  --svgdir freeink-sdk/libs/assets/Icons/lucide/icons \
  --sizes 32 \
  --out src/apps_local/mathquiz/MathQuizIcons.h
echo "wrote src/apps_local/mathquiz/MathQuizIcons.h"
