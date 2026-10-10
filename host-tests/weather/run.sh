#!/bin/sh
# Weather's rules: the settings and forecast files, the once-a-day decision,
# units, WMO codes and the request URLs. No radio, no card, no panel.
#
#   host-tests/weather/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-weather-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I../../src/apps_local/weather \
  ../../src/apps_local/weather/WeatherCore.cpp test_weather.cpp -o "$BUILD_DIR/test_weather"
"$BUILD_DIR/test_weather"
