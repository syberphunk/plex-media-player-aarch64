# plex-media-player-aarch64

aarch64 build of Plex Media Player, packaged as a Flatpak.

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
| zlib (minizip) | 1.3.1 | https://zlib.net |
| org.kde.Platform / org.kde.Sdk | 6.11 | https://invent.kde.org/packaging/flatpak-kde-runtime |
| io.qt.qtwebengine.BaseApp | 6.11 | https://github.com/flathub/io.qt.qtwebengine.BaseApp |

`vendor-deps/` holds copies of the FFmpeg, hwdata, libass, libdisplay-info,
libplacebo, mpv and zlib release archives listed above; the Flatpak manifest
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

## Hardware decoding

mpv is given `hwdec=auto-copy`, so video is decoded off the CPU wherever the
system offers a way to do it. On a device with a stateful V4L2 decoder, the
`*_v4l2m2m` decoder for each codec that decoder accepts is put at the front of
mpv's decoder list; VA-API is used where it is present. Every other codec
decodes in software.

On the Steam Frame this covers H.264, HEVC and VP9, **8 bit only**. Its V4L2
decoder emits `Q08C`, `NV12`, `NV21`, `AB24` and `QC24` and no 10 bit format at
all, so 10 bit video — which is every HDR and Dolby Vision file — decodes in
software. That is a limit of the kernel driver rather than of this build. Vulkan
video decode, NVDEC, VA-API and VDPAU are all absent on that device as well.

The hardware decoder is therefore only offered for streams it can represent.
FFmpeg's V4L2 wrapper does not check a stream against the device: it configures
an 8 bit capture format, decodes a 10 bit stream into it and returns success, so
the result is 8 bit samples labelled BT.2020 PQ rather than an error and a
fallback to software. The bit depth the server reports for the stream, or
failing that the depth encoded in the codec profile name, is compared against
the deepest format the device enumerates, and the hardware decoder is dropped
for that file when the stream is deeper. Dolby Vision is dropped the same way.

What was found and what was used is in the app log:

```
V4L2 stateful decoding available: /dev/video-dec0 (iris_driver) h264,hevc,vp9 up to 8 bit
preferring video decoders h264_v4l2m2m,hevc_v4l2m2m,vp9_v4l2m2m - any other codec falls back to software
V4L2 decoder emits at most 8 bit; deeper streams will use software decoding
not using hevc_v4l2m2m for hevc profile main 10 - stream is 10 bit, decoder emits 8 bit - falling back to software
```

*Hardware decoding* in Plex's own video settings turns all of it off.
`PMP_NO_V4L2_DECODE` skips the V4L2 probe alone and leaves the rest of hardware
decoding in place:

```
flatpak run --env=PMP_NO_V4L2_DECODE=1 io.github.syberphunk.PlexMediaPlayer
```

The sandbox takes `--device=all` to reach the `/dev/video*` nodes, because
Flatpak has no narrower option for them. Access to those nodes is granted by
uaccess ACLs on the seat's own devices, not by group membership, so the sandbox
reaches them as the user who is logged in.

## Settings menu

The application draws its own settings menu over the Plex client, for the
settings Plex's own preferences cannot carry. Open it with:

- `F1` on a keyboard.
- The Menu or Start button on a controller (button 7).
- The cog in the bottom left of the Plex client.

Both bindings are changed from inside the menu, under **Keys and remotes**:
*Key that opens this menu* (`F1`–`F4`, `F8` or `Menu`) and *Gamepad button that
opens this menu* (buttons 2, 3, 6, 7, 8, 9, 10, or none). Gamepad input is read
through SDL.

Up and down move between rows, and the list has a scroll bar. Enter, Return or
Space activates a row; left and right change a drop-down without opening it;
Escape or the controller's Back button closes the menu, as does **Close**. Each
section has its own defaults button, and **Restore all defaults** resets every
setting in the menu.

The sections are:

- **Updates** — covered below.
- **Downloads** — *Save downloads to* chooses Plex's own data folder or the
  system Downloads folder, and *Ask before every download* asks for
  confirmation each time instead of saving straight away.
- **Keys and remotes** — the two bindings above.
- **Display** — *Text size in this menu* (normal, large, larger) and *High
  contrast* (black background, white outlines, brighter highlights). Both apply
  to the menu and to the update messages, not to the Plex client itself.

Settings are stored in
`~/.var/app/io.github.syberphunk.PlexMediaPlayer/data/plexmediaplayer/plexmediaplayer.conf`.

## Updates

The application checks the published repository for a newer build and offers it
in place. The check compares the ostree commit of the running install against
the commit the repository publishes in `update.json`, so a release that keeps
its tag but carries newer contents is still detected. The GitHub release feed
is used when the manifest cannot be reached, and supplies the release notes
either way.

When a newer build is found, an **Update available** screen appears over the
client with the version, the release notes and these choices:

- **Update now** — downloads and installs through the Flatpak portal, with
  progress, then offers **Restart app now** or **On next launch**.
- **Close** — leaves the update; it is offered again next time Plex starts.
- **Skip this version** — does not offer this version again. A later one is
  still offered, and **Stop skipping a version** in the menu undoes it.
- **Never** — turns checking off after a confirmation.

Under **Updates** in the settings menu:

- **Installed** shows the running version.
- **Check for updates** runs a check now.
- **Install update** appears once an update is available.
- **Release notes** shows what changed in the latest release.
- **Check for updates automatically** turns the scheduled check on or off.
- **How often to check** — every time Plex starts (the default), once a day,
  once a week, or only when asked.

The install goes through `flatpak-portal`, which asks
`org.freedesktop.impl.portal.Access` to confirm it. SteamOS Game Mode has no
Access implementation, so the request fails there unless the confirmation is
already stored:

```
flatpak permission-set flatpak updates io.github.syberphunk.PlexMediaPlayer yes
flatpak permission-show io.github.syberphunk.PlexMediaPlayer
```

An update can also be installed from a terminal, which does not use the portal:

```
flatpak update --user io.github.syberphunk.PlexMediaPlayer
```

## Steam shortcut

`packaging/steam/plex-steam-shortcut.py` puts Plex in your Steam library, so you
can start it from Steam like a game. It needs python3 and nothing else.

Run it with no arguments and it asks what to do:

```
python3 packaging/steam/plex-steam-shortcut.py
```

Pick 1 to add Plex, then restart Steam and it is in your library. Steam only
reads its shortcut file when it starts, and overwrites it when it exits, so
changes appear after a restart. Close Steam first if you can; the script works
either way.

Steam has no artwork for a game it did not install, so the tiles stay blank
until you supply images. Put them in the same folder as the script, named for
where you want them to appear:

| File name | Where it appears |
| --- | --- |
| `portrait.png` | the tall tile in your library, 600x900 |
| `capsule.png` | the wide tile in rows and lists, 920x430 |
| `hero.png` | the banner across the top of the Plex page, 1920x620 |
| `logo.png` | the name drawn over that banner, transparent background |
| `icon.png` | the small icon next to the name in list view |

`.jpg` works as well as `.png`. Use whichever of the five you want and leave the
rest out, and the ones you leave out are not touched. Steam files artwork under
a number of its own that it gives the shortcut; the script reads that number out
of Steam's file, so there is nothing for you to look up. Without an `icon` image
the icon the Flatpak itself installs is used. No images are included in this
repository.

If Plex is already in your library, pick 3 and it puts the artwork on the
shortcut that is already there. Removing Plex with 2 deletes its artwork as
well.

Every menu option has a flag, if you would rather skip the menu:

```
python3 packaging/steam/plex-steam-shortcut.py --add
python3 packaging/steam/plex-steam-shortcut.py --artwork
python3 packaging/steam/plex-steam-shortcut.py --list
python3 packaging/steam/plex-steam-shortcut.py --remove
```

Add `-n` to any of them to be told what would happen without anything being
written. `--help` lists the rest, including how to point at one image file
somewhere else.

Steam keeps non-Steam games in `shortcuts.vdf`, a binary file, so the script
rewrites the whole thing. Other games in it come back out as they went in, tags
and play times included, and it copies the file to `shortcuts.vdf.bak` before
its first write.

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
