#!/usr/bin/env bash

set -euo pipefail

TAG=${1:?usage: vendor-libplacebo.sh <tag> [output.tar.gz]}
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT=${2:-$REPO_ROOT/vendor-deps/libplacebo-$TAG.tar.gz}

UPSTREAM=https://code.videolan.org/videolan/libplacebo.git
SUBMODULES=(3rdparty/glad 3rdparty/fast_float 3rdparty/jinja 3rdparty/markupsafe)

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

PREFIX="libplacebo-$TAG"
SRC="$WORK/$PREFIX"

echo "==> cloning $UPSTREAM at $TAG"
git clone --quiet --depth 1 --branch "$TAG" "$UPSTREAM" "$SRC"

echo "==> initialising submodules"
for module in "${SUBMODULES[@]}"; do
  git -C "$SRC" submodule update --init --depth 1 --quiet "$module"
  if [ -z "$(ls -A "$SRC/$module" 2>/dev/null)" ]; then
    echo "$module is empty after checkout" >&2
    exit 1
  fi
done

python3 - "$SRC" <<'EOF'
import os
import sys

root = sys.argv[1]
needed = os.path.join(root, "3rdparty", "jinja", "src", "jinja2", "__init__.py")
if not os.path.isfile(needed):
    sys.exit("missing %s: the GLSL preprocessor imports jinja2 from here"
             % needed)
EOF

echo "==> writing $OUT"
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
tar -czf "$OUT" \
  -C "$WORK" \
  --exclude-vcs \
  --sort=name \
  --owner=0 --group=0 --numeric-owner \
  --mtime='2024-01-01 00:00:00 UTC' \
  "$PREFIX"

echo
sha256sum "$OUT"
tar tzf "$OUT" | grep -c '3rdparty/jinja/' | xargs printf '%s jinja entries\n'
