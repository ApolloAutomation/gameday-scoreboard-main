#!/usr/bin/env bash
# Give every manifest.json under a folder the product name. The web installer
# shows it ("Installing <name>") and Home Assistant titles the panel's update
# with it. build-action fills it from the ESPHome project name,
# ApolloAutomation.gameday-scoreboard, which stays as it is: panels advertise
# it over mDNS and factory_flash.py reads "gameday-scoreboard version" from
# it in the boot log.
# Also puts the manifest's version in the OTA file name.
# check_site_firmware.sh still checks every renamed file.
# Usage: scripts/set_manifest_name.sh <dir>
#        scripts/set_manifest_name.sh --selftest
set -euo pipefail

name="Game Day Scoreboard"

one() {
  local m=$1 dir v p n
  dir=$(dirname "$m")
  jq --arg n "$name" '.name = $n' "$m" > "$m.tmp"
  mv "$m.tmp" "$m"
  v=$(jq -r '.version // empty' "$m")
  if ! [[ "$v" =~ ^[0-9A-Za-z][0-9A-Za-z.+-]*$ ]]; then
    echo "::error::$m has version '$v', which can't go in a file name"
    return 1
  fi
  for p in $(jq -r '.builds[].ota.path // empty' "$m" | sort -u); do
    case "$p" in
      */* | .*)
        echo "::error::$m names OTA file $p outside its folder"
        return 1
        ;;
      *-"$v".ota.bin) continue ;;
      *.ota.bin) n="${p%.ota.bin}-$v.ota.bin" ;;
      *)
        echo "::error::$m names OTA file $p, not a .ota.bin"
        return 1
        ;;
    esac
    if [ ! -f "$dir/$p" ]; then
      echo "::error::$m points at $p, which is not in $dir"
      return 1
    fi
    mv "$dir/$p" "$dir/$n"
    jq --arg o "$p" --arg n "$n" '.builds |= map(if .ota.path == $o then .ota.path = $n else . end)' "$m" > "$m.tmp"
    mv "$m.tmp" "$m"
  done
  echo "$m: name $name, version $v, OTA $(jq -r '[.builds[].ota.path // empty] | join(" ")' "$m")"
}

if [ "${1:-}" = --selftest ]; then
  t=$(mktemp -d)
  trap 'rm -rf "$t"' EXIT
  mkdir -p "$t/a" "$t/b"
  echo one > "$t/a/gameday-esp32s3.ota.bin"
  echo factory > "$t/a/gameday-esp32s3.factory.bin"
  cat > "$t/a/manifest.json" <<'EOF'
{"name": "x", "version": "2026.11.7-beta.191", "builds": [{"chipFamily": "ESP32-S3",
 "ota": {"path": "gameday-esp32s3.ota.bin", "md5": "m"},
 "parts": [{"path": "gameday-esp32s3.factory.bin", "offset": 0}]}]}
EOF
  echo two > "$t/b/gameday-esp32s3.ota.bin"
  echo '{"version": "2026.11.7", "builds": [{"ota": {"path": "gameday-esp32s3.ota.bin"}}]}' > "$t/b/manifest.json"
  bash "$0" "$t" > /dev/null
  bash "$0" "$t" > /dev/null
  got=$(jq -c '[.name, .builds[0].ota.path, .builds[0].parts[0].path]' "$t/a/manifest.json")
  want='["Game Day Scoreboard","gameday-esp32s3-2026.11.7-beta.191.ota.bin","gameday-esp32s3.factory.bin"]'
  [ "$got" = "$want" ] || { echo "FAIL: beta manifest is $got"; exit 1; }
  [ "$(cat "$t/a/gameday-esp32s3-2026.11.7-beta.191.ota.bin")" = one ] || { echo "FAIL: beta OTA not renamed"; exit 1; }
  [ ! -e "$t/a/gameday-esp32s3.ota.bin" ] || { echo "FAIL: old OTA name left behind"; exit 1; }
  [ -f "$t/a/gameday-esp32s3.factory.bin" ] || { echo "FAIL: factory image moved"; exit 1; }
  [ "$(jq -r '.builds[0].ota.path' "$t/b/manifest.json")" = gameday-esp32s3-2026.11.7.ota.bin ] || { echo "FAIL: release manifest"; exit 1; }
  echo '{"version": "1.0", "builds": [{"ota": {"path": "../x.ota.bin"}}]}' > "$t/b/manifest.json"
  if bash "$0" "$t/b" > /dev/null 2>&1; then echo "FAIL: took a path outside the folder"; exit 1; fi
  echo "set_manifest_name selftest ok"
  exit 0
fi

found=0
while IFS= read -r -d '' m; do
  one "$m"
  found=1
done < <(find "$1" -name manifest.json -print0)
if [ $found = 0 ]; then
  echo "::error::no manifest.json under $1"
  exit 1
fi
