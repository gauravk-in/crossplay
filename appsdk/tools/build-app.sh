#!/bin/bash
# Compile one downloadable app to WebAssembly.
#
#   appsdk/tools/build-app.sh <app-dir> <out.wasm> [extra sources...]
#
# <app-dir> holds the app's own .cpp files (its stand-in for an Activity); the
# extra sources are the freestanding cores and screens it shares with the
# built-in app. Every app also links the SDK glue and its own FreeInkUI, so the
# firmware's FreeInkUI version never decides how an installed app looks.
#
# Needs wasi-sdk (https://github.com/WebAssembly/wasi-sdk, release 25):
# WASI_SDK=/path/to/wasi-sdk-25.0-x86_64-linux.
set -euo pipefail

APP_DIR="${1:?usage: build-app.sh <app-dir> <out.wasm> [extra sources...]}"
OUT="${2:?usage: build-app.sh <app-dir> <out.wasm> [extra sources...]}"
shift 2

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WASI_SDK="${WASI_SDK:-/opt/wasi/wasi-sdk-25.0-x86_64-linux}"
CXX="$WASI_SDK/bin/clang++"
[ -x "$CXX" ] || { echo "wasi-sdk not found at $WASI_SDK (set WASI_SDK)" >&2; exit 1; }

FUI="$ROOT/freeink-sdk/libs/ui/FreeInkUI"
FLAGS=(
  --target=wasm32-wasip1 -std=c++20 -Os -fno-exceptions -fno-rtti
  # A reactor has no main(): the firmware calls _initialize (static
  # constructors) and then the cp_app_* exports.
  -mexec-model=reactor
  -I"$ROOT/appsdk/include" -I"$ROOT/appsdk/cpp"
  -I"$FUI/include" -I"$ROOT/freeink-sdk/libs/assets/Icons/include" -I"$ROOT/lib/GfxRenderer"
  -DCROSSPLAY_APP=1
)
LINK=(
  # 64KB of wasm stack; FreeInkUI's deepest layout path wants well over 8KB.
  -Wl,-z,stack-size=65536
  -Wl,--max-memory=4194304
  -Wl,--strip-all
  -Wl,--gc-sections
)

mkdir -p "$(dirname "$OUT")"
"$CXX" "${FLAGS[@]}" "${LINK[@]}" \
  "$ROOT/appsdk/cpp/CrossPlayApp.cpp" "$FUI/src/FreeInkUI.cpp" \
  "$APP_DIR"/*.cpp "$@" -o "$OUT"
echo "built $OUT ($(wc -c < "$OUT") bytes)"
