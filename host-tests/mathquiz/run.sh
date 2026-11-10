#!/bin/sh
# Math Quiz's question generator, history file and chart windows, checked
# without a panel.
#
#   host-tests/mathquiz/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-mathquiz-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I../../src/apps_local/mathquiz test_mathquiz.cpp \
  ../../src/apps_local/mathquiz/MathQuizCore.cpp -o "$BUILD_DIR/test_mathquiz"
"$BUILD_DIR/test_mathquiz"
