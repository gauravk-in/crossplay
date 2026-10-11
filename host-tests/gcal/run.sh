#!/bin/sh
# Builds and runs the Google Calendar tests. No device and no PlatformIO:
# GCalCore is freestanding C++17, and so is the GTasksCore it borrows the
# polling choices from.
#
#   host-tests/gcal/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-gcal-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/gcal/GCalCore.cpp ../../src/apps_local/gtasks/GTasksCore.cpp \
  ../../src/network/DeviceReportCore.cpp \
  test_core.cpp -o "$BUILD_DIR/test_core"
"$BUILD_DIR/test_core"
