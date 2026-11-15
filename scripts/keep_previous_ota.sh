#!/usr/bin/env bash
# Carries the live site's OTA images into a new deploy.
# A panel's offer can predate the deploy by hours.
# Usage: scripts/keep_previous_ota.sh <site dir> [live site URL]
#        scripts/keep_previous_ota.sh --selftest
# Warns instead of failing; a deploy never waits on it.
set -uo pipefail

newer() { # true when $1 sorts after $2
  [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n1)" = "$1" ]
}

otas() { # a manifest's OTA paths and md5s
  jq -c '[.builds[].ota | select(.path) | [.path, (.md5 // "" | ascii_downcase)]] | unique' "$1"
}

keep() {
  local site=$1 base=$2 rel=$3 new tmp nv lv src pv path md5 has n=0
  new="$site/$rel"
  tmp=$(mktemp -d)
  mkdir "$tmp/keep"
  nv=$(jq -r '.version // empty' "$new/manifest.json")
  if ! curl -fsS "$base/$rel/manifest.json?keep=${GITHUB_RUN_ID:-local}" -o "$tmp/live.json" 2> /dev/null ||
     ! lv=$(jq -er '.version' "$tmp/live.json" 2> /dev/null); then
    echo "$rel: no live manifest, nothing to keep"
    rm -rf "$tmp"
    return
  fi
  if newer "$lv" "$nv"; then
    echo "$rel: live $lv is newer than $nv (taken back), so it isn't kept"
    rm -rf "$tmp"
    return
  fi
  if [ "$(otas "$tmp/live.json")" = "$(otas "$new/manifest.json")" ]; then
    # Same build again: carry what it kept.
    if ! curl -fsS "$base/$rel/previous.json" -o "$tmp/src.json" 2> /dev/null; then
      echo "$rel: $nv again, with no previous build kept"
      rm -rf "$tmp"
      return
    fi
    src=$tmp/src.json
  else
    src=$tmp/live.json
  fi
  pv=$(jq -r '.version // empty' "$src")
  while read -r path md5; do
    [ -n "$path" ] || continue
    md5=$(tr 'A-F' 'a-f' <<< "$md5")
    if [[ "$path" == */* || "$path" == .* ]] || ! [[ "$md5" =~ ^[0-9a-f]{32}$ ]]; then
      echo "::warning::$rel: can't keep $pv's $path next to $nv"
      rm -rf "$tmp"
      return
    fi
    if [ -e "$new/$path" ]; then
      [ "$(md5sum < "$new/$path" | cut -d' ' -f1)" = "$md5" ] && continue
      echo "::warning::$rel: $nv's $path has other bytes than $pv's, so an offer read before this deploy fails its MD5 check"
      rm -rf "$tmp"
      return
    fi
    if ! curl -fsS "$base/$rel/$path" -o "$tmp/keep/$path" 2> /dev/null; then
      echo "::warning::$rel: couldn't download $pv's $path, so it isn't kept"
      rm -rf "$tmp"
      return
    fi
    has=$(md5sum < "$tmp/keep/$path" | cut -d' ' -f1)
    if [ "$has" != "$md5" ]; then
      echo "::warning::$rel: $pv's $path has md5 $has, its manifest says $md5; not kept"
      rm -rf "$tmp"
      return
    fi
    n=$((n + 1))
  done < <(jq -r '.builds[].ota | select(.path) | "\(.path) \(.md5 // "-")"' "$src" | sort -u)
  if [ $n = 0 ]; then
    rm -rf "$tmp"
    return
  fi
  mv "$tmp/keep/"* "$new/"
  cp "$src" "$new/previous.json"
  echo "$rel: kept $pv's OTA image beside $nv"
  rm -rf "$tmp"
}

run() {
  local site=$1 base=$2 m
  for m in $(cd "$site" && find firmware -name manifest.json | sort); do
    keep "$site" "$base" "$(dirname "$m")"
  done
}

if [ "${1:-}" = --selftest ]; then
  t=$(mktemp -d)
  trap 'rm -rf "$t"' EXIT
  folder() { # dir version ota-file content
    mkdir -p "$1"
    printf '%s' "$4" > "$1/$3"
    jq -n --arg v "$2" --arg p "$3" --arg m "$(md5sum < "$1/$3" | cut -d' ' -f1)" \
      '{version: $v, builds: [{ota: {path: $p, md5: $m}}]}' > "$1/manifest.json"
  }
  fail() { echo "FAIL: $*"; exit 1; }
  folder "$t/live/firmware/a" 1.0.0 g.ota.bin old
  folder "$t/live/firmware/b" 2.0.0 g-2.0.0.ota.bin taken
  folder "$t/live/firmware/c" 1.0.0 g-1.0.0.ota.bin bad
  jq '.builds[0].ota.md5 = "00000000000000000000000000000000"' "$t/live/firmware/c/manifest.json" > "$t/x" && mv "$t/x" "$t/live/firmware/c/manifest.json"
  folder "$t/new/firmware/a" 1.0.1 g-1.0.1.ota.bin new
  folder "$t/new/firmware/b" 1.0.0 g-1.0.0.ota.bin back
  folder "$t/new/firmware/c" 1.0.1 g-1.0.1.ota.bin new
  folder "$t/new/firmware/d" 1.0.0 g-1.0.0.ota.bin first
  run "$t/new" "file://$t/live" > "$t/log"
  [ "$(cat "$t/new/firmware/a/g.ota.bin" 2> /dev/null)" = old ] || fail "an older live build isn't kept"
  [ "$(jq -r .version "$t/new/firmware/a/previous.json")" = 1.0.0 ] || fail "previous.json"
  [ ! -e "$t/new/firmware/b/g-2.0.0.ota.bin" ] && [ ! -e "$t/new/firmware/b/previous.json" ] || fail "a taken-back build is kept"
  [ ! -e "$t/new/firmware/c/g-1.0.0.ota.bin" ] || fail "a file with the wrong md5 is kept"
  grep -q "::warning::firmware/c" "$t/log" || fail "no warning for the wrong md5"
  [ "$(ls "$t/new/firmware/d")" = "g-1.0.0.ota.bin
manifest.json" ] || fail "a folder new to the site changed"
  # Republishing the same build carries the kept one.
  cp -R "$t/new" "$t/live2"
  folder "$t/new2/firmware/a" 1.0.1 g-1.0.1.ota.bin new
  run "$t/new2" "file://$t/live2" > /dev/null
  [ "$(cat "$t/new2/firmware/a/g.ota.bin" 2> /dev/null)" = old ] || fail "a republish drops the kept build"
  # A rebuild of one version keeps the live bytes.
  folder "$t/live3/firmware/a" 1.0.1 g-1.0.1-aaaa.ota.bin first
  folder "$t/new3/firmware/a" 1.0.1 g-1.0.1-bbbb.ota.bin rebuilt
  run "$t/new3" "file://$t/live3" > /dev/null
  [ "$(cat "$t/new3/firmware/a/g-1.0.1-aaaa.ota.bin" 2> /dev/null)" = first ] || fail "a same-version rebuild drops the live build"
  [ "$(jq -r '.builds[0].ota.path' "$t/new3/firmware/a/previous.json")" = g-1.0.1-aaaa.ota.bin ] || fail "rebuild previous.json"
  # Same bytes, new name: the old name stays.
  folder "$t/new4/firmware/a" 1.0.1 g-1.0.1-cccc.ota.bin first
  run "$t/new4" "file://$t/live3" > /dev/null
  [ "$(cat "$t/new4/firmware/a/g-1.0.1-aaaa.ota.bin" 2> /dev/null)" = first ] || fail "a renamed build drops the old name"
  # One name with other bytes is never overwritten.
  folder "$t/new5/firmware/a" 1.0.1 g-1.0.1-aaaa.ota.bin other
  run "$t/new5" "file://$t/live3" > "$t/log5"
  [ "$(cat "$t/new5/firmware/a/g-1.0.1-aaaa.ota.bin")" = other ] && [ ! -e "$t/new5/firmware/a/previous.json" ] || fail "a reused name was overwritten"
  grep -q "fails its MD5 check" "$t/log5" || fail "no warning for a reused name"
  echo "keep_previous_ota selftest ok"
  exit 0
fi

run "$1" "${2:-${SITE_URL:?SITE_URL or a live site URL}}"
exit 0
