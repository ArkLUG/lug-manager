#!/usr/bin/env bash
# Restore check: can a backup of lug.db be opened by the current app?
#   unshare -rn bash -c "ip link set lo up && scripts/dev/restore_check.sh <backup.db>"
# Never touches the backup itself or anything on the server: it works on a
# copy in a temp folder, offline (same guards as rehearse.sh), then deletes it.
# 1. rehearse.sh on a copy: integrity, migrations up to now, row counts kept,
#    every page loads as an admin and as a member, nothing leaves the machine.
# 2. Money: on the upgraded copy, the LUG bucket plus every chapter's add up
#    to the LUG-wide total (all time).
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
BACKUP=$1
[ -f "$BACKUP" ] || { echo "no such file: $BACKUP"; exit 1; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cp "$BACKUP" "$T/copy.db"; chmod 600 "$T/copy.db"
echo "== backup: $(sqlite3 "$T/copy.db" 'PRAGMA integrity_check' | head -1), schema $(sqlite3 "$T/copy.db" 'SELECT MAX(version) FROM _schema_migrations')"
"$REPO/scripts/dev/rehearse.sh" "$T/copy.db" "$T/work" || exit 1
# Money invariant on the copy, upgraded the same way the app would
for f in "$REPO"/sql/migrations/*.sql; do
  v=$(basename "$f" | cut -d_ -f1 | sed 's/^0*//')
  have=$(sqlite3 "$T/copy.db" "SELECT COUNT(*) FROM _schema_migrations WHERE version=$v")
  [ "$have" = 0 ] && { sqlite3 "$T/copy.db" < "$f" && sqlite3 "$T/copy.db" "INSERT INTO _schema_migrations(version) VALUES($v)"; }
done 2>/dev/null
total=$(sqlite3 "$T/copy.db" "SELECT (SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments) + (SELECT COALESCE(SUM(CASE kind WHEN 'income' THEN amount_cents ELSE -amount_cents END),0) FROM treasury_entries)")
parts=$(sqlite3 "$T/copy.db" "SELECT
  ((SELECT COALESCE(SUM(amount_cents),0) FROM dues_payments) - (SELECT COALESCE(SUM(amount_cents*COALESCE(chapter_pct,0)/100),0) FROM dues_payments WHERE chapter_id IS NOT NULL)
   + (SELECT COALESCE(SUM(CASE kind WHEN 'income' THEN amount_cents ELSE -amount_cents END),0) FROM treasury_entries WHERE chapter_id IS NULL))
  + (SELECT COALESCE(SUM(amount_cents*COALESCE(chapter_pct,0)/100),0) FROM dues_payments WHERE chapter_id IS NOT NULL)
  + (SELECT COALESCE(SUM(CASE kind WHEN 'income' THEN amount_cents ELSE -amount_cents END),0) FROM treasury_entries WHERE chapter_id IS NOT NULL)")
if [ "$total" = "$parts" ]; then echo "== money: LUG + chapters = LUG-wide total ($total cents)"
else echo "== MONEY MISMATCH: LUG-wide $total, LUG + chapters $parts"; exit 1; fi
echo "== restore check done (copy deleted; the backup was not changed)"
