#!/usr/bin/env bash
# Downloads the Tailwind standalone CLI and generates production CSS from templates.
# Run once before starting the server locally.
set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_DIR="$REPO_ROOT/.tailwind"
TW="$BIN_DIR/tailwindcss"
OS="$(uname -s)"
ARCH="$(uname -m)"

mkdir -p "$BIN_DIR" "$REPO_ROOT/src/static"

if [ ! -f "$TW" ]; then
  echo "[css] Downloading Tailwind standalone CLI..."
  case "$OS-$ARCH" in
    Linux-x86_64)   URL_SUFFIX="tailwindcss-linux-x64" ;;
    Linux-aarch64)  URL_SUFFIX="tailwindcss-linux-arm64" ;;
    Darwin-x86_64)  URL_SUFFIX="tailwindcss-macos-x64" ;;
    Darwin-arm64)   URL_SUFFIX="tailwindcss-macos-arm64" ;;
    *)
      echo "[css] Unsupported platform $OS-$ARCH" >&2
      exit 1 ;;
  esac
  curl -sL "https://github.com/tailwindlabs/tailwindcss/releases/latest/download/$URL_SUFFIX" \
    -o "$TW"
  chmod +x "$TW"
fi

echo "[css] Generating production CSS..."
"$TW" \
  -i "$REPO_ROOT/src/css/input.css" \
  --content "$REPO_ROOT/src/templates/**/*.html" \
  -o "$REPO_ROOT/src/static/tailwind.min.css" \
  --minify

echo "[css] Done → src/static/tailwind.min.css"
