# Building

## Source code used

| Component | Version | Upstream |
| --- | --- | --- |
| Plex Media Player | `d9685b2` | https://github.com/plexinc/plex-media-player |
| Plex Desktop web client | 1.115.0.426-4e960a1d | https://www.plex.tv/media-server-downloads/#plex-app |
| FFmpeg | 9.0.2 | https://ffmpeg.org |
| hwdata | v0.412 | https://github.com/vcrhonek/hwdata |
| libass | 0.17.5 | https://github.com/libass/libass |
| libdisplay-info | 0.4.0 | https://gitlab.freedesktop.org/emersion/libdisplay-info |
| libplacebo | v7.360.1 | https://code.videolan.org/videolan/libplacebo |
| mpv (libmpv) | v0.41.0 | https://github.com/mpv-player/mpv |
| OpenXR-SDK (loader) | 1.1.65 | https://github.com/KhronosGroup/OpenXR-SDK |
| zlib (libz, minizip) | 1.4 | https://zlib.net |
| org.kde.Platform / org.kde.Sdk | 6.11 | https://invent.kde.org/packaging/flatpak-kde-runtime |
| io.qt.qtwebengine.BaseApp | 6.11 | https://github.com/flathub/io.qt.qtwebengine.BaseApp |

`vendor-deps/` holds copies of the FFmpeg, hwdata, libass, libdisplay-info,
libplacebo, mpv, OpenXR-SDK and zlib release archives listed above; the Flatpak manifest
builds from those copies, so the build downloads nothing. The Plex Desktop client is
declared as Flatpak `extra-data` and is fetched from plex.tv by
`flatpak install`, on the machine the application is installed on.

Packaging references:

- https://github.com/flathub/tv.plex.PlexDesktop
- https://aur.archlinux.org/packages/plex-desktop

## Build

Requires `flatpak`, `flatpak-builder`, `python3`, `xvfb`, `ffmpeg`, and — when building
aarch64 on another architecture — `qemu-user-static` with its binfmt handlers.
Publishing additionally requires `gh`.

One command installs the runtime, builds, packages, verifies and publishes:

```
./scripts/release.sh --arch aarch64
```

`--no-publish` stops after verification. `--allow-outdated` builds against the
current pins when a newer upstream release exists.

The published release body is rendered from the `<release>` entry in
`packaging/flatpak/io.github.syberphunk.PlexMediaPlayer.metainfo.xml` whose
`version` matches the tag, by `scripts/release-notes.py`. That entry is the only
copy of a release's wording: AppStream reports it as the installed version, the
application shows it under *Release notes*, and the GitHub release carries it.
A tag with no matching entry fails before the build rather than publishing an
empty body. `--notes-file FILE` publishes that file instead.

The individual steps:

```
./scripts/check-sources.py
flatpak install -y flathub org.kde.Platform/aarch64/6.11 \
    org.kde.Sdk/aarch64/6.11 io.qt.qtwebengine.BaseApp/aarch64/6.11
flatpak-builder --arch=aarch64 --force-clean --disable-rofiles-fuse \
    --repo=../fp-repo ../fp-build packaging/flatpak/io.github.syberphunk.PlexMediaPlayer.yml
flatpak build-update-repo --prune ../fp-repo
tar -C ../fp-repo -czf plex-media-player-aarch64-repo.tar.gz .
./scripts/verify-facade-flatpak.sh ../fp-repo aarch64 60
```

`scripts/verify-facade-flatpak.sh` installs from the packaged repository and
runs it headless, and exits non-zero unless the web client completes the host
handshake, so a build whose client never starts cannot be published.

Its last argument is how long the run is given. 60 seconds is enough on native
aarch64, which is what the release workflow uses. Emulated under `qemu-user`,
the client takes far longer to reach the handshake: 240 is the shortest that has
passed, and a slow host needs 480. Too short a window fails a build that is
fine, so raise it rather than trusting the default when cross-building.
`release.sh --verify-seconds` passes the same value through.

The release artifact is an OSTree repository rather than a single-file bundle.
`flatpak install` downloads `extra-data` while pulling from a remote;
`flatpak_dir_install_bundle` does not pull it, so an application that declares
`extra-data` can only be installed from a repository unless the payload is
carried inside the bundle.

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

`.github/workflows/check-sources.yml` runs the check weekly;
`.github/workflows/release.yml` builds, verifies and publishes on a
`v*-aarch64.*` tag, deploys the repository to Pages with
`scripts/stage-pages.sh`, and calls
`.github/workflows/verify-pages.yml`, which installs from the deployed URL.
That workflow also runs on its own against the live site.

