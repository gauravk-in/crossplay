#!/bin/sh
# Builds and runs the Bambu tests: the rules (BambuCore) and the screens
# (BambuScreens, against FreeInkUI with no renderer). Both are freestanding.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-bambu-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/bambu
SAX=../../freeink-sdk/libs/network/JsonSax
SDK=../../freeink-sdk/libs/ui/FreeInkUI
ICONS=../../freeink-sdk/libs/assets/Icons
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC -I$SAX/include test_bambu.cpp $SRC/BambuCore.cpp \
  $SAX/src/StreamingJsonParser.cpp -o "$BUILD_DIR/test_bambu"
"$BUILD_DIR/test_bambu"
if [ -f test_bambu_screens.cpp ]; then
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-comment -I"$SDK/include" -I"$ICONS/include" -I$SRC -I$SAX/include \
    test_bambu_screens.cpp "$SDK/src/FreeInkUI.cpp" $SRC/BambuCore.cpp $SAX/src/StreamingJsonParser.cpp \
    $SRC/BambuScreens.cpp -o "$BUILD_DIR/test_bambu_screens"
  "$BUILD_DIR/test_bambu_screens"
fi
