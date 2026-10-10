#!/bin/sh
# Pomodoro's countdown and dial geometry, checked without a panel.
#
#   host-tests/pomodoro/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-pomodoro-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I../../src/apps_local/pomodoro test_pomodoro.cpp \
  -o "$BUILD_DIR/test_pomodoro"
"$BUILD_DIR/test_pomodoro"
