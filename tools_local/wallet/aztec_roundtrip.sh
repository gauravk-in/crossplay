#!/bin/sh
# Draws Aztec symbols from the Cards encoder and reads them back, which
# host-tests/wallet cannot do because it needs the decoders installed: the
# ZXing build the phone page ships (Node) and zxing-cpp (Python), the second
# because the first cannot read symbols past 22 layers, even its own.
#
#   python3 -m pip install zxing-cpp pillow
#   ./tools_local/wallet/aztec_roundtrip.sh
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${TMPDIR:-/tmp}/cards-aztec-roundtrip"
mkdir -p "$OUT"
SRC="$REPO/src/apps_local/wallet"
"${CXX:-c++}" -std=c++17 -O1 -I"$SRC" "$SRC/WalletAztec.cpp" "$REPO/tools_local/wallet/aztec_dump.cpp" -o "$OUT/aztec_dump"
"$OUT/aztec_dump" > "$OUT/cases.tsv"
node "$REPO/tools_local/wallet/aztec_zxing.js" "$OUT/cases.tsv"
python3 "$REPO/tools_local/wallet/aztec_zxingcpp.py" "$OUT/cases.tsv"
