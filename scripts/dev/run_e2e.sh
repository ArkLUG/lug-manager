#!/usr/bin/env bash
# Run a browser check (tests/e2e/*.py) against a throwaway demo server, offline:
#   unshare -rn bash -c "ip link set lo up && scripts/dev/run_e2e.sh tests/e2e/logo_editor_check.py <out-dir> [role]"
# The script gets: <base-url> <session-token> <out-dir>.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PY=$1; OUT=$2; ROLE=${3:-admin}
VENV=${LUG_VENV:-$HOME/.cache/lug-manager/venv}
W=${TMPDIR:-/tmp}/lm-e2e-$$
read -r PID TOKENS < <("$REPO/scripts/dev/demo_server.sh" "$W" 18089)
TOK=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['$ROLE'])" "$TOKENS")
trap 'kill $PID 2>/dev/null; rm -rf "$W"' EXIT
mkdir -p "$OUT"
SE_AVOID_STATS=true "$VENV/bin/python" "$PY" http://127.0.0.1:18089 "$TOK" "$OUT" 2>&1 | grep -v geckodriver
