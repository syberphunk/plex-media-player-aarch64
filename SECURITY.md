# Security

This repository is an unofficial fork of
[plexinc/plex-media-player](https://github.com/plexinc/plex-media-player),
and the source for the builds attached to its releases. It is not a
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
- Code scanning, secret scanning and push protection are not available on this
  repository, so nothing here is scanned by them.

## What the build checks

- `scripts/check-sources.py` verifies every pinned source against its recorded
  hash and reports when a component has a newer upstream release.
- Flatpak verifies the Plex Desktop client download against the `sha256` and
  `size` pinned in the manifest before `scripts/apply_extra` unpacks it.
- `scripts/verify-web-client.py` then verifies every subresource of the staged
  client against the subresource integrity attributes shipped in its own
  `index.html`, and fails the install on any mismatch.
- `scripts/verify-facade-flatpak.sh` runs the built Flatpak headless and fails
  the release when the client does not complete the host handshake or when a
  subresource integrity check is blocked.
- The Flatpak's sandbox permissions are the `finish-args` list in
  `packaging/flatpak/tv.plex.PlexMediaPlayer.yml`.
- `.github/workflows/release.yml` builds every release asset in GitHub Actions
  and signs it with a build provenance attestation, which ties the bundle to
  the workflow and the commit that produced it:

  ```
  gh attestation verify plex-media-player-aarch64.flatpak \
    --repo syberphunk/plex-media-player-aarch64
  ```

## What it does not do

Chromium reaches the application through `io.qt.qtwebengine.BaseApp` at the
runtime version pinned in the manifest. A release asset is a single-file Flatpak
bundle, so it carries that Chromium until the manifest's runtime version is
raised and the bundle is rebuilt; `flatpak update` refreshes the runtimes
underneath it, not the application. The versions in each release are the ones
listed in [README.md](README.md).
