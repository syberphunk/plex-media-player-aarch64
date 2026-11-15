# Plex Media Player for 64-bit ARM

Plex Media Player built for 64-bit ARM (aarch64) Linux and packaged as a
Flatpak. It is tested on the Steam Frame, where it uses the hardware video
decoder and can show side-by-side and over-under 3D films with depth.

- [Install](#install)
- [Get updates](#get-updates)
- [Uninstall](#uninstall)
- [What works](#what-works)
- [Something is wrong](#something-is-wrong)
- [Build it yourself](#build-it-yourself)

## Install

### On the Steam Frame

You do this once, in the Frame's Desktop Mode. It takes a few minutes.

1. **Switch to Desktop Mode.** Open the dashboard, choose **+**, then
   **Desktop**.

2. **Open a terminal.** Open the application launcher and start **Konsole**.

3. **Install Plex.** Copy this line into the terminal and press Enter:

   ```
   flatpak install --user --from https://syberphunk.github.io/plex-media-player-aarch64/io.github.syberphunk.PlexMediaPlayer.flatpakref
   ```

   Answer **y** to each question. The install downloads the Plex client from
   plex.tv, so the Frame needs to be online.

4. **Let Plex install its own updates.** Still in the terminal, run:

   ```
   flatpak permission-set flatpak updates io.github.syberphunk.PlexMediaPlayer yes
   ```

   Game Mode cannot show the question that normally asks for this, so without
   this line an update offered inside Plex fails there.

5. **Start Plex and add it to your Steam library.** Run:

   ```
   flatpak run io.github.syberphunk.PlexMediaPlayer
   ```

   Sign in to Plex. Click the cog in the bottom-left corner of the Plex window
   to open **Plex Media Player settings**, the menu this app adds. In that
   menu's **Steam** section, choose **Add Steam icon**. Close Plex.

6. **Go back to Game Mode** and restart Steam, or the headset, so Steam picks
   up the new entry. Plex is now in your Steam library.

Start Plex from your Steam library rather than from the **+** menu or from
Desktop Mode. Started from Steam, Plex runs with the performance the Frame
gives games, and 3D films can play with depth.

If you start Plex in Game Mode without the Steam entry, Plex offers to add it
for you.

### On other 64-bit ARM Linux

Install Flatpak from your distribution, run the install line from step 3, and
start Plex from your applications menu. Only the Steam Frame has been tested.

## Get updates

Plex checks for a new version every time it starts. When there is one, an
**Update available** screen shows what changed and offers:

- **Update now** installs it, then offers to restart Plex.
- **Close** asks again next time.
- **Skip this version** waits for the one after.
- **Never** stops checking.

How often Plex checks is under **Updates** in **Plex Media Player settings**.

You can also update from a terminal at any time:

```
flatpak update --user io.github.syberphunk.PlexMediaPlayer
```

If **Update now** fails in Game Mode on the Frame, step 4 of the install was
skipped. Run that line once and try again.

## Uninstall

In a terminal:

```
flatpak uninstall --user -y io.github.syberphunk.PlexMediaPlayer
flatpak remote-delete --user plex-media-player-aarch64
```

The first line removes Plex. The second removes the place updates come from.

The first line keeps your Plex settings and sign-in, so they are still there if
you install Plex again. To delete them as well, use this line instead of the
first one:

```
flatpak uninstall --user -y --delete-data io.github.syberphunk.PlexMediaPlayer
```

On the Frame, the Plex entry stays in your Steam library until you remove it.
In Game Mode, do either of these:

- On Plex's page in your library, choose the cog, then **Manage**, then
  **Remove non-Steam game from your library**.
- In your library's **Non-Steam** list, select Plex, press the menu button
  (three horizontal lines), then choose **Manage**, then **Remove non-Steam
  game from your library**.

## What works

- **Video** in H.264, HEVC and VP9 is decoded by the Frame's hardware decoder.
- **HDR and Dolby Vision** files play in SDR. They are 10-bit video, which the
  Frame's hardware decoder cannot output, so the processor decodes them
  instead.
- **3D films** packed side by side or over and under can play with depth in
  the headset. Turn on **Play 3D files through the headset** in the
  **Stereoscopic 3D** section of **Plex Media Player settings**.
- **Controllers**: the Menu or Start button opens **Plex Media Player
  settings**.

More on each of these is in the [wiki](https://github.com/syberphunk/plex-media-player-aarch64/wiki).

## Something is wrong

See [Troubleshooting](https://github.com/syberphunk/plex-media-player-aarch64/wiki/Troubleshooting).

## Build it yourself

Everything needed to build the Flatpak is in this repository. See
[Building](docs/building.md).

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
