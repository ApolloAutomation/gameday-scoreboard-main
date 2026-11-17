#!/usr/bin/env bash
# Add the beta branch's map landmark files to an assembled site: each
# docs/site/land/<format>/ folder of beta that the site doesn't have yet, so
# a beta panel's device page finds its files before they reach main. A
# format folder is never changed in place (a new format is a new folder), so
# the site's own copy of a format wins.
#
# Never fails the site: a release or a docs change must not wait on the beta.
# Without beta, or when it can't be read, the site goes out with its own
# files only and the run shows a warning.
# Usage: scripts/add_land_files.sh <site dir>   (from a clone with origin)
#        scripts/add_land_files.sh --selftest
set -uo pipefail

add() {
  local site=$1 tmp d n
  tmp=$(mktemp -d)
  if ! git fetch -q --no-tags --depth=1 origin beta 2> "$tmp/fetch.err"; then
    echo "::warning::Couldn't fetch beta, so this deploy has only its own map landmark files: $(head -n1 "$tmp/fetch.err")"
    rm -rf "$tmp"
    return 0
  fi
  mkdir -p "$tmp/x"
  if ! git archive FETCH_HEAD docs/site/land 2> /dev/null | tar -x -C "$tmp/x" 2> /dev/null; then
    echo "beta has no map landmark files"
    rm -rf "$tmp"
    return 0
  fi
  for d in "$tmp"/x/docs/site/land/*/; do
    [ -d "$d" ] || continue
    n=$(basename "$d")
    if [ -e "$site/land/$n" ]; then
      echo "land/$n: this branch's own"
    elif mkdir -p "$site/land" && cp -R "$d" "$site/land/$n"; then
      echo "land/$n: from beta"
    else
      echo "::warning::Couldn't copy beta's land/$n into the site"
    fi
  done
  rm -rf "$tmp"
  return 0
}

if [ "${1:-}" = --selftest ]; then
  t=$(mktemp -d)
  trap 'rm -rf "$t"' EXIT
  export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
  git init -q -b beta "$t/origin"
  mkdir -p "$t/origin/docs/site/land/1" "$t/origin/docs/site/land/2"
  echo beta1 > "$t/origin/docs/site/land/1/index.json"
  echo beta2 > "$t/origin/docs/site/land/2/index.json"
  git -C "$t/origin" add -A && git -C "$t/origin" commit -q -m beta
  git clone -q "$t/origin" "$t/clone"
  mkdir -p "$t/site/land/1"
  echo main1 > "$t/site/land/1/index.json"
  (cd "$t/clone" && add "$t/site") > /dev/null
  [ "$(cat "$t/site/land/1/index.json")" = main1 ] || { echo "FAIL: the site's own format was replaced"; exit 1; }
  [ "$(cat "$t/site/land/2/index.json" 2> /dev/null)" = beta2 ] || { echo "FAIL: beta's new format is missing"; exit 1; }
  # No beta branch: a warning, and the site as it was
  git init -q -b main "$t/empty" && git -C "$t/empty" commit -q --allow-empty -m main
  git clone -q "$t/empty" "$t/clone2" 2> /dev/null
  out=$(cd "$t/clone2" && add "$t/site") || { echo "FAIL: no beta failed the site"; exit 1; }
  grep -q '::warning::' <<< "$out" || { echo "FAIL: no warning without beta"; exit 1; }
  echo "add_land_files selftest ok"
  exit 0
fi

add "$1"
