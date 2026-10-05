#!/usr/bin/env bash
# Upgrade rehearsal on a snapshot of the live database: the new build upgrades
# a copy, offline, and we check migrations, integrity, row counts and pages.
#   unshare -rn bash -c "ip link set lo up && scripts/dev/rehearse.sh <snapshot.db> <work-dir>"
# Three guards against anything leaving the machine: no network namespace,
# LUG_OFFLINE=1, and run from <work-dir> (no .env, no credentials in env).
# The work copy is deleted at the end; delete the snapshot yourself.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SNAP=$1; W=$(realpath -m "$2"); PORT=18090
curl -s -m 3 -o /dev/null https://discord.com && { echo "ABORT: network reachable"; exit 1; }
rm -rf "$W"; mkdir -p "$W/app"; cp "$SNAP" "$W/lug.db"; chmod 600 "$W/lug.db"
ln -s "$REPO/sql" "$W/app/sql"; ln -s "$REPO/src" "$W/app/src"
trap 'kill $PID 2>/dev/null; rm -rf "$W"' EXIT
TABLES=$(sqlite3 "$W/lug.db" "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' AND name NOT IN ('sessions','audit_log','lug_settings','_schema_migrations') ORDER BY name")
counts() { for t in $TABLES; do printf "%s=%s\n" "$t" "$(sqlite3 "$W/lug.db" "SELECT COUNT(*) FROM \"$t\"")"; done; }
echo "== snapshot: $(sqlite3 "$W/lug.db" 'PRAGMA integrity_check' | head -1), schema $(sqlite3 "$W/lug.db" 'SELECT MAX(version) FROM _schema_migrations')"
counts > "$W/before.txt"
(cd "$W/app" && exec env -i HOME="$W" PATH="$PATH" LUG_OFFLINE=1 LUG_DOTENV=0 LUG_DB_PATH="$W/lug.db" LUG_PORT=$PORT \
   LUG_TEMPLATES_DIR="$REPO/src/templates" "$REPO/build/lug_manager" > "$W/new.log" 2>&1) &
PID=$!
for _ in $(seq 1 90); do curl -s -o /dev/null "localhost:$PORT/login" && break; sleep 1; done
grep -q "LUG_OFFLINE=1" "$W/new.log" || { echo "ABORT: server not in offline mode"; exit 1; }
echo "== upgrade: $(grep -c 'Applied version' "$W/new.log") migrations; now schema $(sqlite3 "$W/lug.db" 'SELECT MAX(version) FROM _schema_migrations'); $(sqlite3 "$W/lug.db" 'PRAGMA integrity_check' | head -1); fk problems: $(sqlite3 "$W/lug.db" 'PRAGMA foreign_key_check' | wc -l)"
grep -iE "migration.*(fail|error)" "$W/new.log" | head
grep -E "^\[members\]" "$W/new.log" | head -3
counts > "$W/after.txt"
# Tables that existed before keep their row counts (new tables are fine)
CHANGED=$(diff <(sort "$W/before.txt") <(grep -F -f <(cut -d= -f1 "$W/before.txt" | sed 's/$/=/') "$W/after.txt" | sort) | grep '^[<>]' || true)
[ -z "$CHANGED" ] && echo "== row counts unchanged ($(wc -l < "$W/before.txt") tables)" || { echo "== ROW COUNTS CHANGED:"; echo "$CHANGED"; }
ADMIN=$(sqlite3 "$W/lug.db" "SELECT id FROM members WHERE role='admin' ORDER BY id LIMIT 1")
TOKEN=$(head -c 32 /dev/urandom | xxd -p -c 64)
sqlite3 "$W/lug.db" "INSERT INTO sessions (token, member_id, role, expires_at, token_is_hash) VALUES ('$(printf %s "$TOKEN" | sha256sum | cut -d' ' -f1)', $ADMIN, 'admin', datetime('now','+1 day'), 1)"
EV=$(sqlite3 "$W/lug.db" "SELECT id FROM lug_events ORDER BY start_time DESC LIMIT 1"); MT=$(sqlite3 "$W/lug.db" "SELECT id FROM meetings ORDER BY start_time DESC LIMIT 1")
CH=$(sqlite3 "$W/lug.db" "SELECT id FROM chapters LIMIT 1"); MB=$(sqlite3 "$W/lug.db" "SELECT id FROM members LIMIT 1")
BAD=0
for p in /dashboard /schedule "/schedule?view=calendar" /members /meetings /events /events/$EV /meetings/$MT /members/$MB/view /members/$MB/dues \
         /chapters /chapters/$CH /attendance /attendance/overview /perks /reports/annual "/reports/annual?external=0" /inventory /treasury \
         "/treasury?tab=ledger" /challenges /fancolab /account /help /about-lug-manager /settings /settings/features /settings/site /settings/dues \
         /settings/treasury /settings/reminders /settings/messages /settings/discord-times /settings/backups /settings/roles /audit \
         /events/$EV/calendar.ics /calendar.ics /healthz ${EXTRA_PAGES:-}; do
  c=$(curl -s -o /dev/null -w '%{http_code}' -b "session=$TOKEN" "localhost:$PORT$p")
  [ "$c" = 200 ] || { echo "  $p -> $c"; BAD=$((BAD+1)); }
done
echo "== pages not 200: $BAD"
# A plain member: their pages open, staff/admin pages don't
MEM=$(sqlite3 "$W/lug.db" "SELECT id FROM members WHERE role='member' ORDER BY id LIMIT 1")
if [ -n "$MEM" ]; then
  MTOKEN=$(head -c 32 /dev/urandom | xxd -p -c 64)
  sqlite3 "$W/lug.db" "INSERT INTO sessions (token, member_id, role, expires_at, token_is_hash) VALUES ('$(printf %s "$MTOKEN" | sha256sum | cut -d' ' -f1)', $MEM, 'member', datetime('now','+1 day'), 1)"
  MBAD=0
  for p in /dashboard /schedule /members /chapters /challenges /account; do
    c=$(curl -s -o /dev/null -w '%{http_code}' -b "session=$MTOKEN" "localhost:$PORT$p"); [ "$c" = 200 ] || { echo "  member $p -> $c (want 200)"; MBAD=$((MBAD+1)); }
  done
  for p in /audit /audit.csv /attendance/overview /reports/annual /fancolab /events/all /members.csv /settings/overview /settings/permissions /settings/roles /settings/discord-matches; do
    c=$(curl -s -o /dev/null -w '%{http_code}' -b "session=$MTOKEN" "localhost:$PORT$p"); [ "$c" = 200 ] && { echo "  member $p -> 200 (want refused)"; MBAD=$((MBAD+1)); }
  done
  echo "== member access problems: $MBAD"
fi
echo "== roles: $(sqlite3 "$W/lug.db" "SELECT group_concat(role || '=' || n, ' ') FROM (SELECT role, COUNT(*) n FROM members GROUP BY role)")"
[ -n "$(sqlite3 "$W/lug.db" "SELECT name FROM sqlite_master WHERE name='role_permissions'")" ] && \
  echo "== role_permissions: $(sqlite3 "$W/lug.db" "SELECT group_concat(role || ':' || permission, ' ') FROM role_permissions")"
echo "== outbound attempts blocked: $(grep -c 'blocked outbound' "$W/new.log")"
grep -iE "exception|segfault|abort" "$W/new.log" | grep -v offline | head -3
echo "== done"
