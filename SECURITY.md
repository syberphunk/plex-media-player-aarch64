# Security

This repository is an unofficial fork of
[plexinc/plex-media-player](https://github.com/plexinc/plex-media-player), and
the source for the builds attached to its releases. It is not affiliated with,
endorsed by or supported by Plex, Inc., and it carries no warranty, per sections
11 and 12 of [LICENSE](LICENSE).

## Reporting

Private vulnerability reporting is enabled. Use **Security → Report a
vulnerability** on this repository to send a report that is not publicly
visible. Issues, the wiki and projects are disabled, so that is the channel for
this fork's own code and packaging.

A vulnerability in one of the components below belongs with that project, where
a fix reaches everyone who uses it. It arrives in this build when the pin is
raised and the bundle rebuilt.

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

## Fixes

A fix is applied by raising the pin in
`packaging/flatpak/io.github.syberphunk.PlexMediaPlayer.yml`, or by patching
this fork's own code, and then rebuilding. Every pinned version is listed in
[README.md](README.md), and a release asset is built only from a tagged commit
of this repository.

Where a pin is deliberately held back the reason is recorded with it: zlib stays
at 1.3.1 because CVE-2026-85091 affects 1.3.1.2 through 1.3.2, so
`scripts/check-sources.py --update` reports the newer release and leaves the pin
alone.

## What GitHub checks

- Dependabot alerts are enabled. They cover the GitHub Actions versions pinned
  in `.github/workflows/`; GitHub has no ecosystem for a Flatpak manifest, so
  the pinned C and C++ sources are not covered by them.
- Code scanning runs CodeQL over this fork's own C, C++, Python and workflow
  code.
- Secret scanning and push protection are enabled.

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
  `packaging/flatpak/io.github.syberphunk.PlexMediaPlayer.yml`.
- `.github/workflows/release.yml` builds every release asset in GitHub Actions
  and signs it with a build provenance attestation before publishing it, tying
  the bundle to the workflow and the commit that produced it:

  ```
  gh attestation verify plex-media-player-aarch64.flatpak \
    --repo syberphunk/plex-media-player-aarch64
  ```

## Chromium

`io.qt.qtwebengine.BaseApp` is a build-time base, so QtWebEngine and its
Chromium are built into the application rather than loaded from a runtime.
`flatpak update` refreshes `org.kde.Platform` beneath an installed application
and does not change the Chromium in it. A release carries the base app as it
stood when that release was built, and `scripts/check-sources.py` reports when
the base app has moved on.
