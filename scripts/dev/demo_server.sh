#!/usr/bin/env bash
# Start a throwaway LUG Manager on the seeded demo LUG (scripts/demo/seed.py)
# for screenshots and browser checks, fully offline.
#   scripts/dev/demo_server.sh <work-dir> [port] [extra-sql]
# Prints "PID TOKENS-JSON" on success. Run inside an empty network namespace:
#   unshare -rn bash -c "ip link set lo up && ..."
# Safety: LUG_OFFLINE=1, LUG_DOTENV=0, run from <work-dir> (never the repo, whose
# .env may hold real credentials). <extra-sql> runs on the seeded DB (e.g. to
# stretch names for layout tests).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
W=$(realpath -m "$1"); PORT=${2:-18089}; EXTRA=${3:-}
BIN="$REPO/build/lug_manager"
rm -rf "$W"; mkdir -p "$W/app"
ln -s "$REPO/sql" "$W/app/sql"; ln -s "$REPO/src" "$W/app/src"
start() {
  (cd "$W/app" && exec env -i HOME="$W" PATH="$PATH" LUG_OFFLINE=1 LUG_DOTENV=0 LUG_DB_PATH="$W/lug.db" \
     LUG_PORT=$PORT LUG_TEMPLATES_DIR="$REPO/src/templates" "$BIN" >> "$W/server.log" 2>&1) &
  PID=$!
  for _ in $(seq 1 60); do curl -s -o /dev/null "http://127.0.0.1:$PORT/login" && break; sleep 0.5; done
  grep -q "LUG_OFFLINE=1" "$W/server.log" || { echo "ABORT: server not in offline mode" >&2; kill $PID; exit 1; }
}
start; kill $PID; wait $PID 2>/dev/null || true
TOKENS=$(python3 "$REPO/scripts/demo/seed.py" "$W/lug.db" "$W/uploads")
[ -n "$EXTRA" ] && sqlite3 "$W/lug.db" "$EXTRA"
start
echo "$PID $TOKENS"
