#!/bin/bash
# Regenerate src/apps_local/weather/Weather{,Ui}Icons.h from the two manifests
# beside this script.
#
#   brew install librsvg          # rsvg-convert, the only external dependency
#   ./tools_local/weather/gen_icons.sh
#
# The output is committed, because regenerating needs librsvg and a checkout
# should build without it. Edit a manifest, run this, commit both.
#
# The sky icons come in three sizes: 32 for the ten-day rows, 48 for the hourly
# strip and 112 for today. The chrome only ever draws at 24 and 32.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
GEN=freeink-sdk/libs/assets/Icons/tools/gen_icons.py
SVG=freeink-sdk/libs/assets/Icons/lucide/icons
uv run --quiet --with pillow python "$GEN" --manifest tools_local/weather/icons.txt --svgdir "$SVG" \
  --sizes 32,48,112 --out src/apps_local/weather/WeatherIcons.h
uv run --quiet --with pillow python "$GEN" --manifest tools_local/weather/ui_icons.txt --svgdir "$SVG" \
  --sizes 24,32 --out src/apps_local/weather/WeatherUiIcons.h
echo "wrote src/apps_local/weather/WeatherIcons.h and WeatherUiIcons.h"
