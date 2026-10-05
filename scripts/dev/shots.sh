#!/usr/bin/env bash
# Demo server + screenshots in one go (offline, in an empty network namespace):
#   ~/.claude/scripts/safe-run.sh unshare -rn bash -c "ip link set lo up && scripts/dev/shots.sh <out-dir> [role] paths..."
# role: admin (default) | moderator | member. Set LONG=1 to stretch names
# EXTRA_SQL="..." runs more SQL on the seeded database (e.g. settings).
# (long meeting/event titles, addresses, chapter and member names).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$1; shift
ROLE=admin
case "${1:-}" in admin|moderator|member) ROLE=$1; shift;; esac
VENV=${LUG_VENV:-$HOME/.cache/lug-manager/venv}
W=${TMPDIR:-/tmp}/lm-shots-$$
EXTRA=""
[ "${LONG:-0}" = 1 ] && EXTRA="UPDATE meetings SET title = title || ' Chapter (CAR) meeting - August 2026 with a long name', location = location || ', 3101 Gordon Cooper Blvd, Oklahoma City, OK 73107'; UPDATE lug_events SET title = title || ' Annual Convention and Train Show Spectacular 2026', location = location || ', 3101 Gordon Cooper Blvd, Oklahoma City, OK 73107'; UPDATE chapters SET name = name || ' Chapter of the Greater Region'; UPDATE members SET display_name = display_name || ' Longsurname-Doublebarrel';"
[ -n "${EXTRA_SQL:-}" ] && EXTRA="$EXTRA $EXTRA_SQL"
read -r PID TOKENS < <("$REPO/scripts/dev/demo_server.sh" "$W" 18089 "$EXTRA")
TOK=$(python3 -c "import json,sys;print(json.loads(sys.argv[1])['$ROLE'])" "$TOKENS")
trap 'kill $PID 2>/dev/null; rm -rf "$W"' EXIT
SE_AVOID_STATS=true "$VENV/bin/python" "$REPO/tests/e2e/shots.py" http://127.0.0.1:18089 "$TOK" "$OUT" "$@" 2>&1 | grep -v geckodriver
