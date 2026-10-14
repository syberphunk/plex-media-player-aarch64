#!/usr/bin/env bash

set -euo pipefail

REPO_DIR=${1:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}
BASE_URL=${2:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}
OUT=${3:?usage: stage-pages.sh <repo-dir> <base-url> <out-dir>}

APP_ID=io.github.syberphunk.PlexMediaPlayer
REMOTE_NAME=plex-media-player-aarch64
BASE_URL=${BASE_URL%/}
PUBKEY=$(cd "$(dirname "$0")/.." && pwd)/packaging/flatpak/signing-key.asc

[ -d "$REPO_DIR/objects" ] || {
  echo "$REPO_DIR is not an OSTree repository" >&2
  exit 1
}

command -v flatpak >/dev/null || { echo "flatpak not found" >&2; exit 1; }

GPG_ARGS=()
GPG_KEY_LINE=
if [ -n "${FP_GPG_HOMEDIR:-}" ]; then
  [ -f "$PUBKEY" ] || { echo "$PUBKEY not found" >&2; exit 1; }
  command -v gpg >/dev/null || { echo "gpg not found" >&2; exit 1; }
  FP_GPG_KEY=$(gpg --with-colons --import-options show-only --import "$PUBKEY" \
    | awk -F: '/^fpr:/{print $10; exit}')
  [ -n "$FP_GPG_KEY" ] || { echo "no key fingerprint in $PUBKEY" >&2; exit 1; }
  GPG_ARGS=(--gpg-sign="$FP_GPG_KEY" --gpg-homedir="$FP_GPG_HOMEDIR")
  GPG_KEY_LINE="GPGKey=$(gpg --dearmor < "$PUBKEY" | base64 -w0)"
fi

rm -rf "$OUT"
mkdir -p "$OUT"
cp -a "$REPO_DIR/." "$OUT/"

flatpak build-update-repo "${GPG_ARGS[@]}" --generate-static-deltas --prune "$OUT" >/dev/null

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
${GPG_KEY_LINE}
EOF

cat > "$OUT/$REMOTE_NAME.flatpakrepo" <<EOF
[Flatpak Repo]
Title=Plex Media Player (aarch64)
Url=$BASE_URL/
${GPG_KEY_LINE}
EOF

echo "    $(du -sh "$OUT" | cut -f1)  $OUT"
echo "    $BASE_URL/$APP_ID.flatpakref"
