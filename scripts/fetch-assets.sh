#!/usr/bin/env bash
# scripts/fetch-assets.sh
# Download free assets needed for the client (fonts, etc.)
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FONTS_DIR="$REPO_ROOT/client/assets/fonts"

mkdir -p "$FONTS_DIR"

# ── Font: DejaVu Sans (SIL Open Font Licence) ────────────────────────────────
FONT_TARGET="$FONTS_DIR/main.ttf"
if [ -f "$FONT_TARGET" ]; then
    echo "Font already present: $FONT_TARGET"
else
    echo "Downloading DejaVu Sans..."
    DEJAVU_URL="https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2"
    TMP_DIR="$(mktemp -d)"
    curl -fsSL "$DEJAVU_URL" -o "$TMP_DIR/dejavu.tar.bz2"
    tar -xjf "$TMP_DIR/dejavu.tar.bz2" -C "$TMP_DIR"
    cp "$TMP_DIR/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf" "$FONT_TARGET"
    rm -rf "$TMP_DIR"
    echo "Font saved to $FONT_TARGET"
fi

echo "Done."
