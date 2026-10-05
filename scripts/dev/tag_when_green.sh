#!/usr/bin/env bash
# Tag a release once CI on main is green, then wait for its Docker image.
#   scripts/dev/tag_when_green.sh v2.0.4 [sha]
# Polls GitHub every 3 minutes (only while the API limit has room). The tag's
# image build has hung a few times on its build-push step: if it runs over
# 35 minutes it's cancelled and re-run once. Prints the image digest.
set -u
TAG=$1; SHA=$(git rev-parse "${2:-HEAD}"); VER=${TAG#v}
IMG=ghcr.io/arklug/lug-manager
api_ok() { [ "$(gh api rate_limit -q .resources.core.remaining 2>/dev/null || echo 0)" -gt 50 ]; }
wait_run() {   # <commit-or-branch-flag> -> echoes "id conclusion"
  local run="" s=""
  while :; do
    if api_ok; then
      [ -z "$run" ] && run=$(gh run list "$@" --workflow docker.yml --json databaseId -q '.[0].databaseId' 2>/dev/null)
      [ -n "$run" ] && s=$(gh run view "$run" --json status,conclusion -q '"\(.status) \(.conclusion)"' 2>/dev/null)
      case "$s" in "completed "*) echo "$run ${s#completed }"; return;; esac
    fi
    sleep 180
  done
}
read -r RUN RES < <(wait_run --commit "$SHA")
echo "CI on main $RUN: $RES"; [ "$RES" = success ] || exit 1
git tag -a "$TAG" "$SHA" -m "$TAG" && git push -q origin "$TAG" && echo "TAGGED $TAG at ${SHA:0:7}" || exit 1
digest() {
  local T; T=$(curl -s "https://ghcr.io/token?scope=repository:arklug/lug-manager:pull" | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])" 2>/dev/null) || return
  curl -s -D - -o /dev/null -H "Authorization: Bearer $T" \
    -H 'Accept: application/vnd.oci.image.index.v1+json,application/vnd.docker.distribution.manifest.v2+json,application/vnd.docker.distribution.manifest.list.v2+json' \
    "https://ghcr.io/v2/arklug/lug-manager/manifests/$VER" | grep -i docker-content-digest | tr -d '\r' | awk '{print $2}'
}
start=$(date +%s); rerun=0; TRUN=""
while :; do
  d=$(digest); [ -n "$d" ] && { echo "IMAGE $IMG:$VER $d"; exit 0; }
  if [ $(( $(date +%s) - start )) -gt 2100 ] && [ $rerun = 0 ] && api_ok; then
    TRUN=$(gh run list --branch "$TAG" --workflow docker.yml --json databaseId -q '.[0].databaseId' 2>/dev/null)
    if [ -n "$TRUN" ]; then
      echo "image build over 35 min: cancelling and re-running $TRUN"
      gh run cancel "$TRUN" >/dev/null 2>&1
      for _ in $(seq 1 30); do [ "$(gh run view "$TRUN" --json status -q .status 2>/dev/null)" = completed ] && break; sleep 20; done
      gh run rerun "$TRUN" >/dev/null 2>&1 && rerun=1 && start=$(date +%s)
    fi
  fi
  [ $rerun = 1 ] && [ $(( $(date +%s) - start )) -gt 2700 ] && { echo "STOP: still no image after a re-run"; exit 1; }
  sleep 120
done
