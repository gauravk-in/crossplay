#!/bin/sh
# Builds and runs the Memory rules and recall-score tests. No device and no
# PlatformIO: MemoryCore is freestanding C++17.
#
#   host-tests/memory/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-memory-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/memory
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_memory.cpp $SRC/MemoryCore.cpp -o "$BUILD_DIR/test_memory"
"$BUILD_DIR/test_memory"
