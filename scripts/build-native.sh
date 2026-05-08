#!/usr/bin/env bash
# Build the native client (macOS / Linux)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build/native"

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_WASM=OFF

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Build OK → $BUILD_DIR/client/five_hundred"
