#!/bin/bash
# Regenerate the prompter's text cuts in src/apps_local/prompter/fonts/.
#
#   ./tools_local/prompter/gen_fonts.sh
#
# Noto Sans Bold, because a teleprompter is read from a metre or two away and
# the bold cut survives 1-bit rendering at every size. 1-bit, never --2bit:
# GfxRenderer's BW path paints any coverage, so an antialiased cut floods
# (see tools_local/toybox/gen_toybox_fonts.sh). ASCII, the dashes, the
# curly quotes and the ellipsis; cleanScript() folds Latin-1 accents to ASCII
# rather than paying for a second copy of the alphabet in every cut.
#
# The output is committed, because regenerating needs two Python packages.
set -euo pipefail
REPO="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")/../.." && pwd)"
cd "$REPO"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
uv run --quiet --with fonttools pyftsubset lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Bold.ttf \
  --unicodes="U+0020-007E,U+2013,U+2014,U+2018,U+2019,U+201C,U+201D,U+2026" \
  --output-file="$WORK/sans-bold.ttf"
for size in 14 17 20 26 32 40; do
  out="src/apps_local/prompter/fonts/prompter_sans_${size}.h"
  uv run --quiet --with freetype-py --with fonttools \
    python lib/EpdFont/scripts/fontconvert.py "prompter_sans_${size}" "${size}" "$WORK/sans-bold.ttf" \
    --force-autohint 2>/dev/null | grep -v "extracted$" > "$out"
  sed -i.bak -e "s| \* Command used: .*| * Command used: tools_local/prompter/gen_fonts.sh (fontconvert.py prompter_sans_${size} ${size} NotoSans-Bold subset, --force-autohint)|" "$out"
  rm -f "$out.bak"
  echo "wrote $out"
done
