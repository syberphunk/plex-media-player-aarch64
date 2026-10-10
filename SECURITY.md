# Security

This repository is an unofficial fork of
[plexinc/plex-media-player](https://github.com/plexinc/plex-media-player),
published as the source for the builds attached to its releases. It is not a
supported product: no advisories are issued, no CVE triage is performed, and no
release is promised in response to a report. Issues, the wiki and projects are
disabled, so there is no monitored channel here.

## Where to report

| Subject | Report to |
| --- | --- |
| The Plex client, server or account services | [Plex's security reporting policy](https://support.plex.tv/articles/reporting-security-issues/) |
| Plex Media Player's own code | upstream is archived and unmaintained: https://github.com/plexinc/plex-media-player |
| mpv (libmpv) | https://github.com/mpv-player/mpv/issues |
| libass | https://github.com/libass/libass/issues |
| libplacebo | https://code.videolan.org/videolan/libplacebo/-/issues |
| zlib | https://github.com/madler/zlib/issues |
| Qt and QtWebEngine (Chromium) | https://bugreports.qt.io |
| org.kde.Platform / org.kde.Sdk | https://invent.kde.org/packaging/flatpak-kde-runtime/-/issues |
| io.qt.qtwebengine.BaseApp | https://github.com/flathub/io.qt.qtwebengine.BaseApp/issues |

## What GitHub checks

- Dependabot alerts are enabled. They cover the GitHub Actions versions pinned
  in `.github/workflows/`; GitHub has no ecosystem for a Flatpak manifest, so
  the pinned C and C++ sources are not covered by them.
- `.github/workflows/code-scanning.yml` runs CodeQL weekly over the Actions
  workflows, the Python scripts and the C and C++ sources in `src/`, with
  `.github/codeql-config.yml` as its configuration. It runs without building,
  so its C and C++ results are indicative rather than complete.
- Secret scanning and push protection are enabled.

## What the build checks

- `scripts/check-sources.py` verifies every pinned source against its recorded
  hash and reports when a component has a newer upstream release.
- `scripts/patch-web-client.py` verifies the Plex Desktop client bundle against
  the subresource integrity attribute shipped with it before patching, and
  re-signs the attribute afterwards.
- `scripts/verify-facade-flatpak.sh` runs the built Flatpak headless and fails
  the release when the client does not complete the host handshake or when a
  subresource integrity check is blocked.
- The Flatpak's sandbox permissions are the `finish-args` list in
  `packaging/flatpak/tv.plex.PlexMediaPlayer.yml`.

## What it does not do

Chromium reaches the application through `io.qt.qtwebengine.BaseApp` at the
runtime version pinned in the manifest. A release asset is a single-file Flatpak
bundle, so it carries that Chromium until the manifest's runtime version is
raised and the bundle is rebuilt; `flatpak update` refreshes the runtimes
underneath it, not the application. The versions in each release are the ones
listed in [README.md](README.md).
