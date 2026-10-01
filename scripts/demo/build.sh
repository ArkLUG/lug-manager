#!/usr/bin/env bash
# Build the static GitHub Pages demo.
#   scripts/demo/build.sh <path-to-lug_manager-binary> <out-dir> [site-base]
# site-base is the URL prefix Pages serves under (default /lug-manager).
# Runs a throwaway server with LUG_OFFLINE=1 / LUG_DOTENV=0 from a scratch
# directory (never the repo, whose .env may hold real credentials), seeds the
# fictional Brickton LUG, exports it, and deletes the scratch data.
set -euo pipefail
BIN=$(realpath "$1"); OUT=$(realpath -m "$2"); SITE_BASE=${3:-/lug-manager}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
PORT=${DEMO_PORT:-18095}
trap 'kill $PID 2>/dev/null || true; rm -rf "$WORK"' EXIT

mkdir -p "$WORK/app"
ln -s "$REPO/sql" "$WORK/app/sql"
ln -s "$REPO/src" "$WORK/app/src"

start() {
  (cd "$WORK/app" && exec env -i HOME="$WORK" PATH="$PATH" LUG_OFFLINE=1 LUG_DOTENV=0 \
     LUG_DB_PATH="$WORK/lug.db" LUG_PORT=$PORT LUG_TEMPLATES_DIR="$REPO/src/templates" "$BIN" >> "$WORK/server.log" 2>&1) &
  PID=$!
  for _ in $(seq 1 60); do curl -s -o /dev/null "http://127.0.0.1:$PORT/login" && break; sleep 0.5; done
  grep -q "LUG_OFFLINE=1" "$WORK/server.log" || { echo "server not in offline mode"; cat "$WORK/server.log"; exit 1; }
}

start                                   # creates the schema
kill $PID; wait $PID 2>/dev/null || true
TOKENS=$(python3 "$REPO/scripts/demo/seed.py" "$WORK/lug.db" "$WORK/uploads")
start
python3 "$REPO/scripts/demo/export.py" "http://127.0.0.1:$PORT" "$OUT" "$SITE_BASE" "$TOKENS" \
  "$REPO/src/static" "$WORK/uploads"
echo "[demo] built $(find "$OUT" -name index.html | wc -l) pages in $OUT"
