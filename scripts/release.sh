#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: release.sh [options]

  --arch ARCH        target architecture (default: aarch64)
  --tag TAG          release tag (default: v<version>-aarch64.<next build>)
  --gh-repo O/N      GitHub repository to publish to (default: origin)
  --build-dir DIR    flatpak-builder build dir (default: ../fp-build)
  --repo-dir DIR     ostree repo dir (default: ../fp-repo)
  --verify-seconds N headless run length (default: 60)
  --allow-outdated   build even if a newer upstream release exists
  --no-verify        skip the headless verification gate
  --no-publish       build and verify, then stop before publishing
  --publish-only     publish an already built artifact, skipping steps 1-5
  --offline          skip the source freshness check entirely
EOF
  exit 1
}

publish() {
  command -v gh >/dev/null || { echo "gh not found" >&2; exit 1; }
  [ -n "$GH_REPO" ] || { echo "no GitHub repository: pass --gh-repo" >&2; exit 1; }

  echo "==> step 6/6: publishing $TAG to $GH_REPO"
  BODY="${TAG#v}

$(basename "$ARTIFACT")
"

  if gh release view "$TAG" --repo "$GH_REPO" >/dev/null 2>&1; then
    gh release edit "$TAG" --repo "$GH_REPO" \
      --title "${TAG#v}" --notes "$BODY"
    gh release upload "$TAG" "$ARTIFACT" --repo "$GH_REPO" --clobber
  else
    gh release create "$TAG" "$ARTIFACT" --repo "$GH_REPO" \
      --title "${TAG#v}" --notes "$BODY"
  fi

  echo
  gh release view "$TAG" --repo "$GH_REPO" \
    --json tagName,name,body,assets \
    --jq '{tag: .tagName, title: .name, body: .body, assets: [.assets[].name]}'
}

ARCH=aarch64
TAG=
GH_REPO=
BUILD_DIR=
REPO_DIR=
VERIFY_SECONDS=60
ALLOW_OUTDATED=0
VERIFY=1
PUBLISH=1
PUBLISH_ONLY=0
OFFLINE=0

while [ $# -gt 0 ]; do
  case "$1" in
    --arch) ARCH=${2:?}; shift 2;;
    --tag) TAG=${2:?}; shift 2;;
    --gh-repo) GH_REPO=${2:?}; shift 2;;
    --build-dir) BUILD_DIR=${2:?}; shift 2;;
    --repo-dir) REPO_DIR=${2:?}; shift 2;;
    --verify-seconds) VERIFY_SECONDS=${2:?}; shift 2;;
    --allow-outdated) ALLOW_OUTDATED=1; shift;;
    --no-verify) VERIFY=0; shift;;
    --no-publish) PUBLISH=0; shift;;
    --publish-only) PUBLISH_ONLY=1; shift;;
    --offline) OFFLINE=1; shift;;
    -h|--help) usage;;
    *) echo "unknown option: $1" >&2; usage;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$REPO_ROOT/.." && pwd)"
BUILD_DIR=${BUILD_DIR:-$WORKSPACE/fp-build}
REPO_DIR=${REPO_DIR:-$WORKSPACE/fp-repo}

APP_ID=tv.plex.PlexMediaPlayer
MANIFEST="$REPO_ROOT/packaging/flatpak/$APP_ID.yml"
RUNTIME_VERSION=$(sed -n "s/^runtime-version: *'\{0,1\}\([^']*\)'\{0,1\}$/\1/p" "$MANIFEST")
ARTIFACT="$REPO_ROOT/plex-media-player-$ARCH-repo.tar.gz"
VERIFY_DIR="$WORKSPACE/fp-repo-verify"

for tool in flatpak flatpak-builder python3 ffmpeg; do
  command -v "$tool" >/dev/null || { echo "$tool not found" >&2; exit 1; }
done

MIN_FLATPAK=1.15.1
FLATPAK_VERSION=$(flatpak --version | awk '{print $NF}')
if [ "$(printf '%s\n%s\n' "$MIN_FLATPAK" "$FLATPAK_VERSION" | sort -V | head -1)" != "$MIN_FLATPAK" ]; then
  echo "flatpak $MIN_FLATPAK or newer is required (found $FLATPAK_VERSION)" >&2
  exit 1
fi

version_from_cmake() {
  local file="$REPO_ROOT/CMakeModules/VersionConfiguration.cmake"
  local major minor nano
  major=$(sed -n 's/^set(VERSION_MAJOR \([0-9]*\))$/\1/p' "$file")
  minor=$(sed -n 's/^set(VERSION_MINOR \([0-9]*\))$/\1/p' "$file")
  nano=$(sed -n 's/^set(VERSION_NANO \([0-9]*\))$/\1/p' "$file")
  echo "$major.$minor.$nano"
}

VERSION=$(version_from_cmake)
[ -n "$VERSION" ] || { echo "could not read the version" >&2; exit 1; }

if [ -z "$GH_REPO" ] && command -v git >/dev/null; then
  origin=$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || true)
  GH_REPO=$(echo "$origin" | sed -n 's#.*github.com[:/]\(.*\)#\1#p' | sed 's/\.git$//')
fi

if [ -z "$TAG" ]; then
  next=1
  if [ "$PUBLISH" -eq 1 ] && command -v gh >/dev/null && [ -n "$GH_REPO" ]; then
    existing=$(gh release list --repo "$GH_REPO" --limit 100 \
      --json tagName --jq ".[].tagName | select(startswith(\"v$VERSION-aarch64.\"))" \
      2>/dev/null | sed "s/^v$VERSION-aarch64\.//" | sort -n | tail -1)
    [ -n "$existing" ] && next=$((existing + 1))
  fi
  TAG="v$VERSION-aarch64.$next"
fi

echo "==> plex-media-player $VERSION, $ARCH, tag $TAG"
echo "    manifest   $MANIFEST"
echo "    build dir  $BUILD_DIR"
echo "    repo dir   $REPO_DIR"
echo "    artifact   $ARTIFACT"
[ "$PUBLISH" -eq 1 ] && echo "    publish to $GH_REPO"
echo

if [ "$PUBLISH_ONLY" -eq 1 ]; then
  [ -f "$ARTIFACT" ] || {
    echo "$ARTIFACT not found: build it before --publish-only" >&2
    exit 1
  }
  echo "==> steps 1-5/6: skipped, taking $ARTIFACT as built"
  echo "    $(du -h "$ARTIFACT" | cut -f1)  $(sha256sum "$ARTIFACT" | cut -d' ' -f1)"
  echo
  publish
  exit 0
fi

echo "==> step 1/6: checking pinned sources"
check_args=()
[ "$OFFLINE" -eq 1 ] && check_args+=(--offline)
[ "$ALLOW_OUTDATED" -eq 0 ] && [ "$OFFLINE" -eq 0 ] && check_args+=(--fail-on-outdated)
set +e
"$REPO_ROOT/scripts/check-sources.py" "${check_args[@]}"
rc=$?
set -e
if [ "$rc" -eq 2 ]; then
  echo
  echo "A newer upstream release exists. Either take it:" >&2
  echo "    scripts/check-sources.py --update" >&2
  echo "or build against the current pins with --allow-outdated." >&2
  exit 2
fi
[ "$rc" -eq 0 ] || exit "$rc"

echo
echo "==> step 2/6: installing the $ARCH runtime"
flatpak remote-add --if-not-exists --user \
  flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install -y --user --noninteractive \
  "org.kde.Platform/$ARCH/$RUNTIME_VERSION" \
  "org.kde.Sdk/$ARCH/$RUNTIME_VERSION" \
  "io.qt.qtwebengine.BaseApp/$ARCH/$RUNTIME_VERSION"

if [ "$ARCH" != "$(flatpak --default-arch)" ] && \
   [ ! -e "/proc/sys/fs/binfmt_misc/qemu-$ARCH" ]; then
  echo "no binfmt_misc registration for qemu-$ARCH: this host cannot run" >&2
  echo "$ARCH binaries. Install qemu-user-static and its binfmt handlers." >&2
  exit 1
fi

echo
echo "==> step 3/6: building"
flatpak-builder --arch="$ARCH" --force-clean --disable-rofiles-fuse \
  --repo="$REPO_DIR" "$BUILD_DIR" "$MANIFEST"

echo
echo "==> step 4/6: packaging the repository"
[ -f "$REPO_DIR/refs/heads/app/$APP_ID/$ARCH/master" ] || {
  echo "$REPO_DIR holds no app/$APP_ID/$ARCH/master" >&2
  exit 1
}
flatpak build-update-repo --prune "$REPO_DIR" >/dev/null
rm -f "$ARTIFACT"
tar -C "$REPO_DIR" -czf "$ARTIFACT" .
echo "    $(du -h "$ARTIFACT" | cut -f1)  $(sha256sum "$ARTIFACT" | cut -d' ' -f1)"

echo
if [ "$VERIFY" -eq 1 ]; then
  echo "==> step 5/6: verifying"
  rm -rf "$VERIFY_DIR"
  mkdir -p "$VERIFY_DIR"
  tar -C "$VERIFY_DIR" -xzf "$ARTIFACT"
  "$REPO_ROOT/scripts/verify-facade-flatpak.sh" "$VERIFY_DIR" "$ARCH" "$VERIFY_SECONDS"
else
  echo "==> step 5/6: verification skipped"
fi

echo
if [ "$PUBLISH" -eq 0 ]; then
  echo "==> step 6/6: publishing skipped"
  echo
  echo "Artifact: $ARTIFACT"
  exit 0
fi

publish
