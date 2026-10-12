#!/usr/bin/env bash

set -euo pipefail

REPO_DIR=${1:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}
BASE_URL=${2:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}
OUT=${3:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}

APP_ID=tv.plex.PlexMediaPlayer
REMOTE_NAME=plex-media-player-aarch64
BASE_URL=${BASE_URL%/}

[ -d "$REPO_DIR/objects" ] || {
  echo "$REPO_DIR is not an OSTree repository" >&2
  exit 1
}

command -v flatpak >/dev/null || { echo "flatpak not found" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$REPO_DIR/." "$OUT/"

flatpak build-update-repo --generate-static-deltas --prune "$OUT" >/dev/null

touch "$OUT/.nojekyll"

cat > "$OUT/$APP_ID.flatpakref" <<EOF
[Flatpak Ref]
Name=$APP_ID
Branch=master
Title=Plex Media Player
Url=$BASE_URL/
SuggestRemoteName=$REMOTE_NAME
RuntimeRepo=https://dl.flathub.org/repo/flathub.flatpakrepo
IsRuntime=false
EOF

cat > "$OUT/$REMOTE_NAME.flatpakrepo" <<EOF
[Flatpak Repo]
Title=Plex Media Player (aarch64)
Url=$BASE_URL/
EOF

echo "    $(du -sh "$OUT" | cut -f1)  $OUT"
echo "    $BASE_URL/$APP_ID.flatpakref"
