#!/usr/bin/env bash
# Build and run the Go server
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/server/five_hundred_server"

mkdir -p "$(dirname "$BIN")"
(cd "$ROOT/server" && go build -o "$BIN" ./cmd/server)

echo "Server built → $BIN"
exec "$BIN" "$@"
