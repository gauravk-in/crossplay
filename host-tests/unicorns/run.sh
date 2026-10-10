#!/bin/sh
# Builds and runs the Unicorns rules tests. No device and no PlatformIO:
# UnicornCore is freestanding C++17.
#
#   host-tests/unicorns/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-unicorns-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/unicorns
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_unicorns.cpp -o "$BUILD_DIR/test_unicorns"
"$BUILD_DIR/test_unicorns"
