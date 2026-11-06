#!/bin/bash
# The beta channel's version for a build of the beta branch (build.yml).
#
#   scripts/beta_version.sh <version from firmware/gameday-common.yaml> <run number>
#   scripts/beta_version.sh --selftest
#
# Panels put every beta before the release with its number (auto_update.h):
# 2026.10.1-beta.20 comes after 2026.10.1-beta.19 and before 2026.10.1.
# So a version with no release tag yet, the next release on its way through
# beta, gets "-beta.<run>". A version that is already released, as on beta
# between releases, takes the next patch number first: 2026.10.1 builds
# 2026.10.2-beta.<run>, newer than the release panels run and older than any
# release after it. Fails when it can't tell whether the version is
# released, so a beta never goes out under the wrong number.
set -euo pipefail

name() { # version, run number, released (1) or not (0)
  local v=$1 run=$2
  if [ "$3" = 1 ]; then
    v="${v%.*}.$((10#${v##*.} + 1))"
  fi
  echo "$v-beta.$run"
}

if [ "${1:-}" = --selftest ]; then
  fail=0
  check() {
    local got
    got=$(name "$1" "$2" "$3")
    if [ "$got" != "$4" ]; then
      echo "FAIL: $1 run $2 released=$3 gave $got, want $4"
      fail=1
    fi
  }
  check 2026.10.1 20 0 2026.10.1-beta.20
  check 2026.10.1 21 1 2026.10.2-beta.21
  check 2026.10.9 5 1 2026.10.10-beta.5
  check 2026.11.0 7 1 2026.11.1-beta.7
  check 2026.11.0 7 0 2026.11.0-beta.7
  [ "$fail" = 0 ] && echo "selftest ok"
  exit "$fail"
fi

v=$1
run=$2
if ! [[ "$v" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || ! [[ "$run" =~ ^[0-9]+$ ]]; then
  echo "::error file=firmware/gameday-common.yaml::version '$v' or run '$run' isn't a release number like 2026.10.1 and a run number" >&2
  exit 1
fi
rc=0
git ls-remote --exit-code --tags origin "refs/tags/v$v" > /dev/null || rc=$?
case $rc in
  0) name "$v" "$run" 1 ;;
  2) name "$v" "$run" 0 ;;
  *)
    echo "::error::Couldn't read the release tags from origin (git ls-remote exit $rc), so the beta's version is unknown" >&2
    exit 1
    ;;
esac
