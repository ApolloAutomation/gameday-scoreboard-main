#!/usr/bin/env bash
# Wait until the live site serves this deploy's update feed.
# Every manifest's version and OTA md5s, and each OTA image; and each
# map landmark index (land/<format>/index.json), which the device page
# reads from another origin, so on http(s) it needs Pages' CORS header.
# Usage: scripts/check_live_feed.sh <site dir> [live site URL]
#        scripts/check_live_feed.sh --selftest
# LIVE_TRIES (36) times LIVE_WAIT (20 s) bounds the wait.
set -uo pipefail

once() {
  local site=$1 base=$2 try=$3 ok=$4 m want got path md5 has
  for m in $(cd "$site" && find firmware -name manifest.json | sort); do
    want=$(jq -c '[.version, [.builds[].ota.md5]]' "$site/$m")
    got=$(curl -fsS "$base/$m?live=${GITHUB_RUN_ID:-local}-$try" 2> /dev/null | jq -c '[.version, [.builds[].ota.md5]]' 2> /dev/null)
    if [ "$got" != "$want" ]; then
      echo "try $try: $base/$m has ${got:-nothing}, want $want"
      return 1
    fi
    while read -r path md5; do
      grep -qxF "$m $path" "$ok" && continue
      # Plain URL, the way a panel asks.
      has=$(curl -fsS "$base/$(dirname "$m")/$path" 2> /dev/null | md5sum | cut -d' ' -f1)
      if [ "$has" != "$(tr 'A-F' 'a-f' <<< "$md5")" ]; then
        echo "try $try: $base/$(dirname "$m")/$path has md5 $has, want $md5"
        return 1
      fi
      echo "$m $path" >> "$ok"
    done < <(jq -r '.builds[].ota | "\(.path) \(.md5)"' "$site/$m")
  done
  for m in $(cd "$site" && find land -name index.json 2> /dev/null | sort); do
    grep -qxF "$m" "$ok" && continue
    if ! curl -fsS "$base/$m?live=${GITHUB_RUN_ID:-local}-$try" 2> /dev/null | cmp -s - "$site/$m"; then
      echo "try $try: $base/$m isn't this deploy's"
      return 1
    fi
    case $base in
      http*)
        if ! curl -fsS -o /dev/null -D - -H "Origin: http://192.168.1.50" "$base/$m" 2> /dev/null |
          grep -qi '^access-control-allow-origin: \*'; then
          echo "try $try: $base/$m has no Access-Control-Allow-Origin: *, so device pages can't read the map landmarks"
          return 1
        fi
        ;;
    esac
    echo "$m" >> "$ok"
  done
}

check() {
  local site=$1 base=$2 tries=${LIVE_TRIES:-36} wait=${LIVE_WAIT:-20} ok i n
  n=$(cd "$site" && find firmware -name manifest.json | wc -l | tr -d ' ')
  if [ "$n" = 0 ]; then
    echo "::error::No manifests under $site/firmware"
    return 1
  fi
  ok=$(mktemp)
  for i in $(seq 1 "$tries"); do
    if once "$site" "$base" "$i" "$ok"; then
      echo "Live at $base: $n manifests and their OTA images"
      rm -f "$ok"
      return 0
    fi
    [ "$i" = "$tries" ] || sleep "$wait"
  done
  rm -f "$ok"
  echo "::error::$base doesn't serve this deploy's update feed after $tries tries; panels aren't offered it"
  return 1
}

if [ "${1:-}" = --selftest ]; then
  t=$(mktemp -d)
  trap 'rm -rf "$t"' EXIT
  mkdir -p "$t/site/firmware/a"
  echo image > "$t/site/firmware/a/g-1.ota.bin"
  jq -n --arg m "$(md5sum < "$t/site/firmware/a/g-1.ota.bin" | cut -d' ' -f1)" \
    '{version: "1", builds: [{ota: {path: "g-1.ota.bin", md5: $m}}]}' > "$t/site/firmware/a/manifest.json"
  export LIVE_TRIES=2 LIVE_WAIT=0
  check "$t/site" "file://$t/site" > /dev/null || { echo "FAIL: the same files aren't live"; exit 1; }
  mkdir -p "$t/old/firmware/a"
  cp "$t/site/firmware/a/g-1.ota.bin" "$t/old/firmware/a/"
  jq '.version = "0"' "$t/site/firmware/a/manifest.json" > "$t/old/firmware/a/manifest.json"
  if check "$t/site" "file://$t/old" > /dev/null; then echo "FAIL: an old manifest passed"; exit 1; fi
  cp "$t/site/firmware/a/manifest.json" "$t/old/firmware/a/"
  echo other > "$t/old/firmware/a/g-1.ota.bin"
  if check "$t/site" "file://$t/old" > /dev/null; then echo "FAIL: a different image passed"; exit 1; fi
  rm "$t/old/firmware/a/g-1.ota.bin"
  if check "$t/site" "file://$t/old" > /dev/null; then echo "FAIL: a missing image passed"; exit 1; fi
  mkdir -p "$t/site/land/1"
  echo '{"format": 1}' > "$t/site/land/1/index.json"
  check "$t/site" "file://$t/site" > /dev/null || { echo "FAIL: the same landmark index isn't live"; exit 1; }
  rm -rf "$t/old" && cp -R "$t/site" "$t/old"
  echo '{"format": 0}' > "$t/old/land/1/index.json"
  if check "$t/site" "file://$t/old" > /dev/null; then echo "FAIL: an old landmark index passed"; exit 1; fi
  echo "check_live_feed selftest ok"
  exit 0
fi

check "$1" "${2:-${SITE_URL:?SITE_URL or a live site URL}}"
