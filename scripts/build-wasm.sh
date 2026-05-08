#!/usr/bin/env bash
# Build the WebAssembly client via Emscripten
# Requires: emsdk installed and `source emsdk_env.sh` already run (or EMSDK set).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/wasm"

if ! command -v emcc &>/dev/null; then
    echo "Error: emcc not found. Run 'source <emsdk>/emsdk_env.sh' first." >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/emscripten.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_WASM=ON

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Build OK → $BUILD_DIR/client/five_hundred.html"
echo "Serve with: python3 -m http.server -d $BUILD_DIR/client 8000"
