# Privacy

This build adds no data collection, analytics, telemetry or crash reporting of
its own, and contacts no server operated by the publisher of this repository.

It makes network connections to:

| Destination | When | Why |
| --- | --- | --- |
| plex.tv | `flatpak install` | downloading the Plex Desktop client declared as `extra-data` in the Flatpak manifest |
| Plex accounts and relay services | in use | the Plex Desktop client's own sign-in and server discovery |
| Plex Media Server instances | in use | playback |
| flathub.org | `flatpak install`, `flatpak update` | the `org.kde.Platform` runtime and `io.qt.qtwebengine.BaseApp` |

Everything sent to Plex is sent by Plex's own client and server software, and is
governed by Plex's privacy policy:
https://www.plex.tv/about/privacy-legal/privacy-preferences/

Local state is written under `~/.var/app/tv.plex.PlexMediaPlayer/`: the Plex
Desktop client's cookies, cache and local storage, the application's settings
and its log file. Removing that directory removes all of it.
