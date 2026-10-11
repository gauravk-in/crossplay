#!/bin/sh
# Prompter's settings, paging, page timer and page-turner decoding, checked
# without a panel or a radio.
#
#   host-tests/prompter/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-prompter-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I../../src/apps_local/prompter test_prompter.cpp \
  ../../src/apps_local/prompter/PrompterCore.cpp -o "$BUILD_DIR/test_prompter"
"$BUILD_DIR/test_prompter"
