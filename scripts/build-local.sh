#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$REPO_ROOT/.." && pwd)"
WEB_CLIENT_DIR="$WORKSPACE/vendor/web-client"
BUILD_DIR="$WORKSPACE/build"

if [[ ! -f "$WEB_CLIENT_DIR/buildid.cmake" ]]; then
  echo "==> Fetching web client"
  "$REPO_ROOT/scripts/fetch-web-client.sh" "$WEB_CLIENT_DIR"
fi

echo "==> Configuring"
cmake -GNinja \
  -DQTROOT=/usr \
  -DCMAKE_BUILD_TYPE=Release \
  -DWEB_CLIENT_LOCAL_DIR="$WEB_CLIENT_DIR" \
  -B "$BUILD_DIR" -S "$REPO_ROOT" "$@"

echo "==> Building"
ninja -C "$BUILD_DIR"

echo
echo "Built: $BUILD_DIR/src/plexmediaplayer"
