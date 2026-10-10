#!/usr/bin/env bash

set -euo pipefail

BUILD_ID="183-045db5be50e175"
TV_VERSION="4.29.6-045db5b"
DESKTOP_VERSION="4.29.2-e50e175"

TV_SHA1="04251fe3f125ca3b17b615ca74597239389e6356"
DESKTOP_SHA1="944c3dcb515c81026ad6e58b1845c6a3fff3a988"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$REPO_ROOT/../vendor/web-client}"

BASE="https://artifacts.plex.tv/web-client-pmp/$BUILD_ID"
WB="https://web.archive.org/web"

mkdir -p "$DEST"

fetch() {
  local timestamp="$1" name="$2"
  if [[ -s "$DEST/$name" ]]; then
    echo "  have    $name"
    return
  fi
  echo "  fetch   $name"
  curl -fsSL --retry 3 --retry-delay 2 -o "$DEST/$name.part" "$WB/$timestamp/$BASE/$name"
  mv "$DEST/$name.part" "$DEST/$name"
}

verify() {
  local name="$1" expected="$2"
  local actual
  actual="$(sha1sum "$DEST/$name" | cut -d' ' -f1)"
  if [[ "$actual" != "$expected" ]]; then
    echo "ERROR: $name sha1 mismatch" >&2
    echo "  expected $expected" >&2
    echo "  actual   $actual" >&2
    exit 1
  fi
  echo "  ok      $name  ($actual)"
}

echo "Fetching web-client $BUILD_ID into $DEST"

fetch 20240805080455 "buildid.cmake"
fetch 20250524181135 "web-client-tv-$TV_VERSION.tar.xz"
fetch 20240805080459 "web-client-tv-$TV_VERSION.tar.xz.sha1"
fetch 20250524102033 "web-client-desktop-$DESKTOP_VERSION.tar.xz"
fetch 20230607234903 "web-client-desktop-$DESKTOP_VERSION.tar.xz.sha1"

echo "Verifying:"
verify "web-client-tv-$TV_VERSION.tar.xz"           "$TV_SHA1"
verify "web-client-desktop-$DESKTOP_VERSION.tar.xz" "$DESKTOP_SHA1"

for pair in "web-client-tv-$TV_VERSION.tar.xz:$TV_SHA1" \
            "web-client-desktop-$DESKTOP_VERSION.tar.xz:$DESKTOP_SHA1"; do
  name="${pair%:*}"; expected="${pair#*:}"
  if [[ -s "$DEST/$name.sha1" ]]; then
    sidecar="$(tr -d '[:space:]' < "$DEST/$name.sha1" | cut -c1-40)"
    if [[ "$sidecar" != "$expected" ]]; then
      echo "ERROR: sidecar $name.sha1 ($sidecar) disagrees with the expected $expected" >&2
      exit 1
    fi
  fi
done

cat <<MSG

Done. Configure with:

  cmake -GNinja -DQTROOT=/usr \\
        -DWEB_CLIENT_LOCAL_DIR=$DEST \\
        -DCMAKE_BUILD_TYPE=Release <path-to-source>
MSG
