# plex-media-player-arm64

aarch64 build of Plex Media Player, packaged as a Flatpak.

## Source code used

| Component | Version | Upstream |
| --- | --- | --- |
| Plex Media Player | `d9685b2` | https://github.com/plexinc/plex-media-player |
| Plex Desktop web client | 1.115.0.426-4e960a1d | https://www.plex.tv/media-server-downloads/#plex-app |
| http-parser | 2.9.4 (`ec8b5ee`) | https://github.com/nodejs/http-parser |
| libass | 0.17.5 | https://github.com/libass/libass |
| libplacebo | v7.349.0 | https://code.videolan.org/videolan/libplacebo |
| mpv (libmpv) | v0.39.0 | https://github.com/mpv-player/mpv |
| zlib (minizip) | 1.3.1 | https://zlib.net |
| org.kde.Platform / org.kde.Sdk | 6.11 | https://invent.kde.org/packaging/flatpak-kde-runtime |
| io.qt.qtwebengine.BaseApp | 6.11 | https://github.com/flathub/io.qt.qtwebengine.BaseApp |

`vendor-deps/` holds copies of the libass, libplacebo, mpv and zlib release
archives listed above, used as the Flatpak manifest's mirrors.
`external/qhttp/3rdparty/http-parser/` holds the http-parser copy listed above.

Packaging references:

- https://github.com/flathub/tv.plex.PlexDesktop
- https://aur.archlinux.org/packages/plex-desktop

## Build

Requires `flatpak`, `flatpak-builder`, `python3`, `xvfb`, `ffmpeg`, and — when building
aarch64 on another architecture — `qemu-user-static` with its binfmt handlers.
Publishing additionally requires `gh`.

One command installs the runtime, builds, bundles, verifies and publishes:

```
./scripts/release.sh --arch aarch64
```

`--no-publish` stops after verification. `--allow-outdated` builds against the
current pins when a newer upstream release exists.

The individual steps:

```
./scripts/check-sources.py
flatpak install -y flathub org.kde.Platform/aarch64/6.11 \
    org.kde.Sdk/aarch64/6.11 io.qt.qtwebengine.BaseApp/aarch64/6.11
flatpak-builder --arch=aarch64 --force-clean --disable-rofiles-fuse \
    --repo=../fp-repo ../fp-build packaging/flatpak/tv.plex.PlexMediaPlayer.yml
flatpak build-bundle --arch=aarch64 \
    --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
    ../fp-repo plex-media-player-aarch64.flatpak tv.plex.PlexMediaPlayer
./scripts/verify-facade-flatpak.sh ../fp-repo aarch64 60
```

`scripts/verify-facade-flatpak.sh` runs the bundle headless and exits non-zero
unless the web client completes the host handshake, so a build whose client
never starts cannot be published.

## Sources

`scripts/check-sources.py` verifies every pinned source and reports newer
upstream releases:

```
./scripts/check-sources.py             # pins, mirrors and upstream versions
./scripts/check-sources.py --deep      # hash every remote copy
./scripts/check-sources.py --offline   # in-repo copies only
./scripts/check-sources.py --update    # take newer releases and re-pin
```

`--update` downloads each newer release, rewrites the manifest's URL, mirror
and `sha256`, replaces the copy in `vendor-deps/`, and rewrites the version
table above. libplacebo is vendored by `scripts/vendor-libplacebo.sh`, which
clones the tag with the submodules its GLSL preprocessor needs and writes a
reproducible archive.

zlib is held at 1.3.1. CVE-2026-85091 affects 1.3.1.2 through 1.3.2, so
`--update` reports the newer release and leaves the pin alone.

`.github/workflows/check-sources.yml` runs the check weekly;
`.github/workflows/release.yml` builds, verifies and publishes on a
`v*-arm64.*` tag.

## Install

```
flatpak install --user plex-media-player-aarch64.flatpak
flatpak run tv.plex.PlexMediaPlayer
```

## License

GPL-2.0, per `LICENSE`. The Plex Desktop web client is proprietary to Plex,
Inc., is not redistributed here, and is downloaded from plex.tv during the
build.
