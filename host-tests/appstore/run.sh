#!/bin/sh
# Downloadable apps: API compatibility, manifests, the catalog and SHA-256,
# checked without a device. Also checks that the API version is the same in
# the SDK header, the firmware and docs/app-api/compat.json, and that every
# function the SDK header declares is one the firmware links.
#
#   host-tests/appstore/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-appstore-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -Werror \
  ../../src/apps_local/appstore/AppStoreCore.cpp test_core.cpp -o "$BUILD_DIR/test_core"
"$BUILD_DIR/test_core"

python3 check_api.py
python3 -m unittest discover -s ../../server/appstore/tests -t ../../server/appstore -q
