#!/usr/bin/env python3

import argparse
import os
import shutil
import struct
import sys
import zlib
from glob import glob

APPNAME = "Plex"
EXE = "/usr/bin/flatpak"
FLATPAK_ID = "io.github.syberphunk.PlexMediaPlayer"
LAUNCH = "run " + FLATPAK_ID

MAP, STR, INT, END = 0x00, 0x01, 0x02, 0x08
FLOAT, PTR, WSTR, UINT64, INT64 = 0x03, 0x04, 0x05, 0x07, 0x0A

ARTWORK = [
    ("capsule", "%s.png", "wide capsule, 920x430"),
    ("portrait", "%sp.png", "portrait capsule, 600x900"),
    ("hero", "%s_hero.png", "hero banner, 1920x620"),
    ("logo", "%s_logo.png", "logo, transparent png"),
]

ICON_SEARCH = [
    "~/.local/share/flatpak/exports/share/icons/hicolor/*/apps/%s.*" % FLATPAK_ID,
    "/var/lib/flatpak/exports/share/icons/hicolor/*/apps/%s.*" % FLATPAK_ID,
]

HELP = """\
Add the Plex flatpak to Steam as a non-Steam shortcut.

Steam reads shortcuts.vdf when it starts and writes its own copy back when it
exits, so restart Steam afterwards, and shut it down beforehand where that is
possible. Check the result with --list.
"""


def read_cstring(buf, pos):
    end = buf.index(b"\x00", pos)
    return buf[pos:end].decode("utf-8", "replace"), end + 1


def parse_map(buf, pos):
    out = {}
    while True:
        kind = buf[pos]
        pos += 1
        if kind == END:
            return out, pos
        key, pos = read_cstring(buf, pos)
        if kind == MAP:
            out[key], pos = parse_map(buf, pos)
        elif kind in (STR, WSTR):
            out[key], pos = read_cstring(buf, pos)
        elif kind in (INT, PTR, FLOAT):
            out[key] = struct.unpack_from("<i", buf, pos)[0]
            pos += 4
        elif kind in (UINT64, INT64):
            out[key] = struct.unpack_from("<q", buf, pos)[0]
            pos += 8
        else:
            raise ValueError("unknown vdf type 0x%02x at offset %d" % (kind, pos - 1))


def write_map(data):
    out = bytearray()
    for key, value in data.items():
        name = key.encode("utf-8") + b"\x00"
        if isinstance(value, dict):
            out += bytes([MAP]) + name + write_map(value) + bytes([END])
        elif isinstance(value, bool):
            out += bytes([INT]) + name + struct.pack("<i", int(value))
        elif isinstance(value, int):
            out += bytes([INT]) + name + struct.pack("<i", value)
        else:
            out += bytes([STR]) + name + str(value).encode("utf-8") + b"\x00"
    return bytes(out)


def serialise(shortcuts):
    return bytes([MAP]) + b"shortcuts\x00" + write_map(shortcuts) + bytes([END]) + bytes([END])


def load(path):
    if not os.path.exists(path):
        return {}
    with open(path, "rb") as handle:
        buf = handle.read()
    if not buf:
        return {}
    if buf[0] != MAP:
        raise ValueError("%s: not a binary vdf" % path)
    root, pos = read_cstring(buf, 1)
    if root.lower() != "shortcuts":
        raise ValueError("%s: unexpected root %r" % (path, root))
    shortcuts, _ = parse_map(buf, pos)
    return shortcuts


def store(path, shortcuts, dry_run):
    blob = serialise(shortcuts)
    check, _ = parse_map(blob, 1 + len(b"shortcuts\x00"))
    if len(check) != len(shortcuts):
        raise ValueError("round-trip check failed")

    if dry_run:
        print("  dry run, %s not written" % path)
        return

    if os.path.exists(path) and not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")
        print("  backup written to %s.bak" % path)

    tmp = path + ".new"
    with open(tmp, "wb") as handle:
        handle.write(blob)
    os.replace(tmp, path)
    print("  wrote %s" % path)


def signed_appid(exe, appname):
    return (zlib.crc32((exe + appname).encode("utf-8")) | 0x80000000) - 0x100000000


def unsigned_appid(exe, appname):
    return zlib.crc32((exe + appname).encode("utf-8")) | 0x80000000


def quoted_exe():
    return '"%s"' % EXE


def find_configs():
    roots = [
        "~/.steam/steam/userdata",
        "~/.local/share/Steam/userdata",
        "~/.steam/root/userdata",
    ]
    seen = {}
    for root in roots:
        for path in glob(os.path.join(os.path.expanduser(root), "*", "config")):
            if os.path.basename(os.path.dirname(path)) == "0":
                continue
            seen[os.path.realpath(path)] = path
    return sorted(seen.values())


def find_icon():
    for pattern in ICON_SEARCH:
        for path in sorted(glob(os.path.expanduser(pattern))):
            if path.lower().endswith((".png", ".jpg", ".ico")):
                return path
    for pattern in ICON_SEARCH:
        for path in sorted(glob(os.path.expanduser(pattern))):
            return path
    return ""


def steam_is_running():
    for path in glob("/proc/[0-9]*/comm"):
        try:
            with open(path) as handle:
                if handle.read().strip() == "steam":
                    return True
        except OSError:
            continue
    return False


def ours(entry):
    if not isinstance(entry, dict):
        return False
    if entry.get("LaunchOptions") == LAUNCH or FLATPAK_ID in str(entry.get("Exe", "")):
        return True
    exe = quoted_exe()
    return entry.get("appid") in (signed_appid(exe, APPNAME),
                                  unsigned_appid(exe, APPNAME))


def new_entry(icon):
    exe = quoted_exe()
    return {
        "appid": signed_appid(exe, APPNAME),
        "AppName": APPNAME,
        "Exe": exe,
        "StartDir": '"%s"' % os.path.dirname(EXE),
        "icon": icon,
        "ShortcutPath": "",
        "LaunchOptions": LAUNCH,
        "IsHidden": 0,
        "AllowDesktopConfig": 1,
        "AllowOverlay": 1,
        "OpenVR": 0,
        "Devkit": 0,
        "DevkitGameID": "",
        "DevkitOverrideAppID": 0,
        "LastPlayTime": 0,
        "FlatpakAppID": "",
        "tags": {},
    }


def do_add(config, icon, dry_run, force):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)

    existing = [index for index, entry in shortcuts.items() if ours(entry)]
    if existing and not force:
        print("  already present as entry %s, nothing to do" % ", ".join(existing))
        return False
    for index in existing:
        del shortcuts[index]

    index = 0
    while str(index) in shortcuts:
        index += 1
    shortcuts[str(index)] = new_entry(icon)

    print("  entry %s: %s %s" % (index, EXE, LAUNCH))
    print("  icon: %s" % (icon or "none"))
    store(path, shortcuts, dry_run)
    return not dry_run


def do_remove(config, dry_run):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)

    existing = [index for index, entry in shortcuts.items() if ours(entry)]
    if not existing:
        print("  no Plex entry found, nothing to do")
        return False

    kept = [entry for index, entry in shortcuts.items() if index not in existing]
    reindexed = {str(position): entry for position, entry in enumerate(kept)}

    print("  removing entry %s" % ", ".join(existing))
    store(path, reindexed, dry_run)
    return not dry_run


def do_list(config):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)
    if not shortcuts:
        print("  no shortcuts")
        return False
    for index, entry in shortcuts.items():
        if not isinstance(entry, dict):
            continue
        mark = "*" if ours(entry) else " "
        print("  %s %-3s %-28s %s %s" % (mark, index, entry.get("AppName", ""),
                                         entry.get("Exe", ""), entry.get("LaunchOptions", "")))
    return False


def do_artwork(config, files, dry_run):
    grid = os.path.join(config, "grid")
    appid = unsigned_appid(quoted_exe(), APPNAME)
    print("%s" % grid)

    wrote = False
    for name, template, _ in ARTWORK:
        source = files.get(name)
        if not source:
            continue
        source = os.path.expanduser(source)
        if not os.path.isfile(source):
            print("  %s: %s not found" % (name, source))
            continue
        target = os.path.join(grid, template % appid)
        print("  %s -> %s" % (source, target))
        if dry_run:
            continue
        os.makedirs(grid, exist_ok=True)
        shutil.copyfile(source, target)
        wrote = True

    if not wrote and not dry_run:
        print("  nothing to do")
    return wrote


def prompt_artwork():
    print()
    print("Supply your own image files. Leave a line blank to skip it.")
    files = {}
    for name, _, description in ARTWORK:
        answer = input("  %s (%s): " % (name, description)).strip()
        if answer:
            files[name] = answer
    return files


def menu(configs, dry_run):
    changed = False
    while True:
        print()
        print("Plex, Steam shortcut")
        if steam_is_running():
            print("Steam is running. Restart it afterwards, then check with 4.")
        print()
        print("  1) Add the shortcut")
        print("  2) Remove the shortcut")
        print("  3) Set artwork from local image files")
        print("  4) Show the shortcuts that are installed")
        print("  q) Quit")
        print()

        try:
            choice = input("Choice: ").strip().lower()
        except EOFError:
            choice = "q"

        if choice == "1":
            icon = find_icon()
            for config in configs:
                changed |= do_add(config, icon, dry_run, False)
        elif choice == "2":
            for config in configs:
                changed |= do_remove(config, dry_run)
        elif choice == "3":
            files = prompt_artwork()
            for config in configs:
                changed |= do_artwork(config, files, dry_run)
        elif choice == "4":
            for config in configs:
                do_list(config)
        elif choice in ("q", "quit", "exit"):
            if changed:
                print()
                print("Restart Steam to pick up the changes.")
            return 0
        else:
            print("Not a choice.")


def main():
    parser = argparse.ArgumentParser(
        description=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--add", action="store_true")
    parser.add_argument("--remove", action="store_true")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--icon", help="an image file for the shortcut icon")
    for name, _, description in ARTWORK:
        parser.add_argument("--" + name, help=description)
    parser.add_argument("-f", "--force", action="store_true",
                        help="replace an existing entry instead of leaving it alone")
    parser.add_argument("-n", "--dry-run", action="store_true")
    parser.add_argument("--config", action="append",
                        help="a Steam userdata config directory, repeatable")
    args = parser.parse_args()

    configs = args.config or find_configs()
    if not configs:
        print("No Steam userdata config directory found.", file=sys.stderr)
        print("Pass one with --config, or sign in to Steam once first.", file=sys.stderr)
        return 1

    files = {name: getattr(args, name) for name, _, _ in ARTWORK}
    batch = args.add or args.remove or args.list or any(files.values())

    if not batch:
        return menu(configs, args.dry_run)

    if steam_is_running():
        print("Steam is running. Restart it afterwards, then check with --list.",
              file=sys.stderr)

    changed = False
    for config in configs:
        if args.list:
            do_list(config)
        if args.remove:
            changed |= do_remove(config, args.dry_run)
        if args.add:
            icon = args.icon if args.icon is not None else find_icon()
            changed |= do_add(config, icon, args.dry_run, args.force)
        if any(files.values()):
            changed |= do_artwork(config, files, args.dry_run)

    if changed:
        print("Restart Steam to pick up the changes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
