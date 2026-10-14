# plex-media-player-aarch64

aarch64 build of Plex Media Player, packaged as a Flatpak.

## Source code used

| Component | Version | Upstream |
| --- | --- | --- |
| Plex Media Player | `d9685b2` | https://github.com/plexinc/plex-media-player |
| Plex Desktop web client | 1.115.0.426-4e960a1d | https://www.plex.tv/media-server-downloads/#plex-app |
| hwdata | v0.412 | https://github.com/vcrhonek/hwdata |
| libass | 0.17.5 | https://github.com/libass/libass |
| libdisplay-info | 0.4.0 | https://gitlab.freedesktop.org/emersion/libdisplay-info |
| libplacebo | v7.360.1 | https://code.videolan.org/videolan/libplacebo |
| mpv (libmpv) | v0.41.0 | https://github.com/mpv-player/mpv |
| zlib (minizip) | 1.3.1 | https://zlib.net |
| org.kde.Platform / org.kde.Sdk | 6.11 | https://invent.kde.org/packaging/flatpak-kde-runtime |
| io.qt.qtwebengine.BaseApp | 6.11 | https://github.com/flathub/io.qt.qtwebengine.BaseApp |

`vendor-deps/` holds copies of the hwdata, libass, libdisplay-info, libplacebo,
mpv and zlib release archives listed above; the Flatpak manifest builds from
those copies, so the build downloads nothing. The Plex Desktop client is
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

zlib is held at 1.3.1. CVE-2026-85091 affects 1.3.1.2 through 1.3.2, so
`--update` reports the newer release and leaves the pin alone.

`.github/workflows/check-sources.yml` runs the check weekly;
`.github/workflows/release.yml` builds, verifies and publishes on a
`v*-aarch64.*` tag, deploys the repository to Pages with
`scripts/stage-pages.sh`, and calls
`.github/workflows/verify-pages.yml`, which installs from the deployed URL.
That workflow also runs on its own against the live site.

## Install

```
flatpak install --user --from \
    https://syberphunk.github.io/plex-media-player-aarch64/io.github.syberphunk.PlexMediaPlayer.flatpakref
flatpak run io.github.syberphunk.PlexMediaPlayer
```

Once installed, the remote is in place and later releases follow from it:

```
flatpak update io.github.syberphunk.PlexMediaPlayer
```

`--reinstall` is not honoured when installing `--from` a flatpakref; repeating
the install command fails with `branch master already installed`. To replace an
installation rather than update it, remove both the application and the remote
the flatpakref created:

```
flatpak uninstall --user -y io.github.syberphunk.PlexMediaPlayer
flatpak remote-delete --user plex-media-player-aarch64
```

Uninstalling the application leaves the remote behind, and a remote keeps the
`gpg-verify` setting it was created with. A remote added before the repository
was signed therefore stays unverified, whatever is installed through it later;
deleting it lets the flatpakref recreate it with the key.

The release artifact is the same repository as a tarball, for installing
without the published one:

```
mkdir -p plex-media-player-aarch64-repo
tar -xzf plex-media-player-aarch64-repo.tar.gz -C plex-media-player-aarch64-repo
gpg --dearmor < packaging/flatpak/signing-key.asc > signing-key.gpg
flatpak remote-add --user --gpg-import=signing-key.gpg plex-media-player-aarch64 \
    "file://$PWD/plex-media-player-aarch64-repo"
flatpak install --user plex-media-player-aarch64 io.github.syberphunk.PlexMediaPlayer
flatpak run io.github.syberphunk.PlexMediaPlayer
```

This takes the same remote name the flatpakref uses, and both carry
`app/io.github.syberphunk.PlexMediaPlayer/aarch64/master`, so the two cannot
be installed at once: remove the published remote first, as above.

The repository and its summary are signed with `packaging/flatpak/signing-key.asc`,
whose fingerprint is `5B835A64 C562436E BF9BBDDB B9634B40 3133AD1F`. The
flatpakref and flatpakrepo carry the same key, so an install from the published
URL verifies without any further step.

The install step downloads the Plex Desktop client from plex.tv, so it needs
network access. It verifies the download against the `sha256` and `size` pinned
in the manifest, then stages and verifies it on the target; a mismatch fails
the install.

## Steam shortcut

`packaging/steam/plex-steam-shortcut.py` adds the installed Flatpak to Steam as
a non-Steam shortcut, calling
`flatpak run io.github.syberphunk.PlexMediaPlayer`. It needs only python3.

Steam reads `shortcuts.vdf` when it starts and writes its own copy back when it
exits, so restart Steam afterwards, and shut it down beforehand where that is
possible. `--list` shows what is in the file.

```
python3 packaging/steam/plex-steam-shortcut.py
```

With no arguments it presents a menu: add the shortcut, remove it, set artwork,
list what is installed, quit. The same operations are available non
interactively:

```
python3 packaging/steam/plex-steam-shortcut.py --add
python3 packaging/steam/plex-steam-shortcut.py --remove
python3 packaging/steam/plex-steam-shortcut.py --list
python3 packaging/steam/plex-steam-shortcut.py -n --add    # dry run
```

`--add` leaves an existing entry alone; `-f` replaces it. `--config` names a
Steam `userdata/<id>/config` directory, repeatable, when the search paths
(`~/.steam/steam`, `~/.local/share/Steam`, `~/.steam/root`) do not apply.

The icon is taken from the Flatpak's own exported icon when one is installed, or
from `--icon FILE`. That icon is the small one beside the name in a list; the
library tiles are separate images, which Steam reads from
`userdata/<id>/config/grid`, named after the shortcut's app id:

| Option | File | Size | Where it appears |
| --- | --- | --- | --- |
| `--portrait` | `<appid>p.png` | 600x900 | the tall tile in the library grid |
| `--capsule` | `<appid>.png` | 920x430 | the wide tile in rows and lists |
| `--hero` | `<appid>_hero.png` | 1920x620 | the banner across the top of the page |
| `--logo` | `<appid>_logo.png` | transparent png | over the banner, in place of the name |

Any image file is accepted and copied under the name in the table; the app id is
derived from the shortcut, so the script works it out. Each option is
independent, and menu option 3 asks for the same four.

```
python3 packaging/steam/plex-steam-shortcut.py \
    --portrait tall.png --capsule wide.png --hero banner.png --logo logo.png
```

No artwork is shipped in this repository.

`shortcuts.vdf` is Valve's binary VDF format, so the script parses the existing
file and rewrites it, preserving other entries and their tags. It backs up to
`shortcuts.vdf.bak` on first write, re-parses what it is about to write before
writing it, and replaces the file atomically.

## License

GPL-2.0, per `LICENSE`. Copyright holders are listed in `COPYRIGHT`.

The Plex Desktop web client is proprietary to Plex,
Inc. Neither this repository nor a release asset contains any part of it. It is
declared as Flatpak `extra-data`, so it is downloaded from plex.tv at install
time by the machine it is installed on, and `scripts/apply_extra` stages it
there, verifying every subresource against the integrity attributes shipped in
the client's own `index.html`. Its files are used exactly as published; the
`qwebchannel.js` that Qt 6 provides is injected by the host at document
creation rather than by altering the client.

Plex's terms of service are at
https://www.plex.tv/about/privacy-legal/plex-terms-of-service/ and its privacy
policy at https://www.plex.tv/about/privacy-legal/privacy-preferences/.
`PRIVACY.md` records what this build collects.
