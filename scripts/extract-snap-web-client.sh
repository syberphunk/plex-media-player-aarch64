#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(dirname "$REPO_ROOT")"
SRC="${1:-$WORKSPACE/vendor/snap-extract}"
DEST="${2:-$WORKSPACE/vendor/web-client-modern}"

SRC="$(cd "$SRC" 2>/dev/null && pwd || echo "$SRC")"

WC="$SRC/resources/web-client"
if [[ ! -f $WC/index.html ]]; then
  echo "error: no web client at $WC" >&2
  echo "       unsquashfs the snap into $SRC first." >&2
  exit 1
fi

VERSION=$(ls "$WC" | sed -n 's/.*-plex-\([0-9][^-]*\)-.*/\1/p' | sort -u | head -1)
SHA=$(cat "$WC/current_sha.txt" 2>/dev/null || echo unknown)
if [[ -z $VERSION ]]; then
  echo "error: could not determine the web client version from $WC" >&2
  exit 1
fi

echo "==> web client $VERSION ($SHA)"
for flavour in desktop tv; do
  rm -rf "${DEST:?}/${flavour:?}"
  mkdir -p "${DEST:?}/${flavour:?}"
  cp -a "$WC/." "$DEST/$flavour/"
done

cat > "$DEST/buildid.cmake" <<EOF
set(WEB_CLIENT_VERSION_DESKTOP "$VERSION")
set(WEB_CLIENT_VERSION_TV "$VERSION")
set(WEB_CLIENT_BUILD_ID "snap-$SHA")
EOF

cat > "$DEST/PROVENANCE.txt" <<EOF
Plex Web $VERSION
upstream sha: $SHA
staged from: $SRC
staged on:   $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

echo "==> staged to $DEST"
du -sh "$DEST"
