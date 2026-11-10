#!/bin/sh
# Builds and runs the Checkmate puzzle tests: every bundled puzzle replayed
# through the device's own chess rules, plus the session, history and chart
# rules. No device and no PlatformIO needed.
#
#   host-tests/chesspuz/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-chesspuz-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror ../../src/apps_local/chess/ChessCore.cpp \
  ../../src/apps_local/chess/ChessEngine.cpp ../../src/apps_local/chesspuz/ChessPuzzleCore.cpp test_chesspuz.cpp \
  -o "$BUILD_DIR/test_chesspuz"
"$BUILD_DIR/test_chesspuz"
