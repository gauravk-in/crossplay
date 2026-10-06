#!/bin/sh
# Builds and runs the Google Tasks tests. No device and no PlatformIO:
# GTasksCore is freestanding C++17.
#
#   host-tests/gtasks/run.sh
#
# Nothing but the standard library is on the include path, so if the cache
# format, the merge or the poll schedule ever reaches for HalStorage,
# ArduinoJson or the network, this build fails instead of the logic quietly
# becoming device-only.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-gtasks-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/gtasks/GTasksCore.cpp \
  test_core.cpp -o "$BUILD_DIR/test_core"
"$BUILD_DIR/test_core"
