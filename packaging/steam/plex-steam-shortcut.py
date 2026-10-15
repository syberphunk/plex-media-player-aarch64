#!/usr/bin/env python3

import argparse
import os
import re
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

IMAGE_EXTENSIONS = (".png", ".jpg", ".jpeg")

ARTWORK = [
    ("portrait", "%sp", "the tall tile in the library, 600x900"),
    ("capsule", "%s", "the wide tile in rows and lists, 920x430"),
    ("hero", "%s_hero", "the banner across the top of the page, 1920x620"),
    ("logo", "%s_logo", "the name drawn over the banner, a transparent png"),
]

ICON_SEARCH = [
    "~/.local/share/flatpak/exports/share/icons/hicolor/*/apps/%s.*" % FLATPAK_ID,
    "/var/lib/flatpak/exports/share/icons/hicolor/*/apps/%s.*" % FLATPAK_ID,
]

HELP = """\
Add the Plex flatpak to Steam as a non-Steam game.

Run it with no arguments for a menu. For artwork on the library page, save your
images in this folder named portrait, capsule, hero, logo or icon, as .png or
.jpg, and they are used when the shortcut is added.

Steam reads shortcuts.vdf when it starts and writes its own copy back when it
exits, so restart Steam afterwards, and shut it down beforehand where that is
possible.
"""


class VdfInt(int):
    def __new__(cls, value, kind):
        self = super().__new__(cls, value)
        self.kind = kind
        return self


class VdfFloat(float):
    pass


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
        elif kind == FLOAT:
            out[key] = VdfFloat(struct.unpack_from("<f", buf, pos)[0])
            pos += 4
        elif kind in (INT, PTR):
            out[key] = VdfInt(struct.unpack_from("<i", buf, pos)[0], kind)
            pos += 4
        elif kind in (UINT64, INT64):
            out[key] = VdfInt(struct.unpack_from("<q", buf, pos)[0], kind)
            pos += 8
        else:
            raise ValueError("unknown vdf type 0x%02x at offset %d" % (kind, pos - 1))


def write_map(data):
    out = bytearray()
    for key, value in data.items():
        name = key.encode("utf-8") + b"\x00"
        if isinstance(value, dict):
            out += bytes([MAP]) + name + write_map(value) + bytes([END])
        elif isinstance(value, VdfFloat):
            out += bytes([FLOAT]) + name + struct.pack("<f", value)
        elif isinstance(value, VdfInt):
            if value.kind in (UINT64, INT64):
                out += bytes([value.kind]) + name + struct.pack("<q", int(value))
            else:
                out += bytes([value.kind]) + name + struct.pack("<i", int(value))
        elif isinstance(value, int):
            out += bytes([INT]) + name + struct.pack("<i", int(value))
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
    if len(check) != len(shortcuts) or serialise(check) != blob:
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


def icon_size(path):
    found = re.search(r"/(\d+)x(\d+)/", path)
    return int(found.group(1)) if found else 0


def find_icon():
    here = find_image("icon")
    if here:
        return here
    for pattern in ICON_SEARCH:
        raster = [p for p in glob(os.path.expanduser(pattern))
                  if p.lower().endswith((".png", ".jpg", ".ico"))]
        if raster:
            return sorted(raster, key=lambda p: (icon_size(p), p))[-1]
    for pattern in ICON_SEARCH:
        for path in sorted(glob(os.path.expanduser(pattern))):
            return path
    return ""


def image_folders():
    here = os.path.dirname(os.path.abspath(__file__))
    cwd = os.getcwd()
    if os.path.realpath(cwd) == os.path.realpath(here):
        return [here]
    return [cwd, here]


def find_image(name):
    for folder in image_folders():
        for extension in IMAGE_EXTENSIONS:
            path = os.path.join(folder, name + extension)
            if os.path.isfile(path):
                return path
    return ""


def find_artwork():
    found = {}
    for name, _, _ in ARTWORK:
        path = find_image(name)
        if path:
            found[name] = path
    return found


def steam_is_running():
    for path in glob("/proc/[0-9]*/comm"):
        try:
            with open(path) as handle:
                if handle.read().strip() == "steam":
                    return True
        except OSError:
            continue
    return False


def entry_appid(entry):
    value = entry.get("appid")
    if not isinstance(value, int) or isinstance(value, bool):
        return None
    return value & 0xFFFFFFFF


def ours(entry):
    if not isinstance(entry, dict):
        return False
    where = "%s %s" % (entry.get("Exe", ""), entry.get("LaunchOptions", ""))
    if FLATPAK_ID in where:
        return True
    return entry_appid(entry) == unsigned_appid(quoted_exe(), APPNAME)


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


def shortcut_appid(config):
    shortcuts = load(os.path.join(config, "shortcuts.vdf"))
    for entry in shortcuts.values():
        if ours(entry):
            found = entry_appid(entry)
            if found:
                return found
    return None


def do_add(config, icon, dry_run, force):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)

    existing = [index for index, entry in shortcuts.items() if ours(entry)]
    if existing and not force:
        print("  Plex is already there as entry %s, nothing to do"
              % ", ".join(existing))
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


def do_icon(config, icon, dry_run):
    path = os.path.join(config, "shortcuts.vdf")
    shortcuts = load(path)
    existing = [index for index, entry in shortcuts.items() if ours(entry)]
    if not existing:
        return do_add(config, icon, dry_run, False)

    print("%s" % path)
    for index in existing:
        shortcuts[index]["icon"] = icon
    print("  icon: %s" % (icon or "none"))
    store(path, shortcuts, dry_run)
    return not dry_run


def do_remove(config, dry_run):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)

    existing = [index for index, entry in shortcuts.items() if ours(entry)]
    if not existing:
        print("  no Plex entry here, nothing to do")
        return False

    appid = shortcut_appid(config)
    kept = [entry for index, entry in shortcuts.items() if index not in existing]
    reindexed = {str(position): entry for position, entry in enumerate(kept)}

    print("  removing entry %s" % ", ".join(existing))
    store(path, reindexed, dry_run)
    if appid is not None:
        drop_artwork(config, appid, dry_run)
    return not dry_run


def drop_artwork(config, appid, dry_run):
    grid = os.path.join(config, "grid")
    for _, template, _ in ARTWORK:
        for extension in IMAGE_EXTENSIONS:
            path = os.path.join(grid, (template % appid) + extension)
            if not os.path.exists(path):
                continue
            print("  removing %s" % path)
            if not dry_run:
                os.remove(path)


def do_list(config):
    path = os.path.join(config, "shortcuts.vdf")
    print("%s" % path)
    shortcuts = load(path)
    if not shortcuts:
        print("  nothing in it")
        return False
    for index, entry in shortcuts.items():
        if not isinstance(entry, dict):
            continue
        mark = "*" if ours(entry) else " "
        print("  %s %-3s %-28s %s %s" % (mark, index, entry.get("AppName", ""),
                                         entry.get("Exe", ""), entry.get("LaunchOptions", "")))
    return False


def do_artwork(config, files, dry_run):
    appid = shortcut_appid(config)
    if appid is None:
        if not dry_run:
            print("%s" % config)
            print("  no Plex shortcut here yet, so add it first and the artwork")
            print("  will go with it")
            return False
        appid = unsigned_appid(quoted_exe(), APPNAME)

    grid = os.path.join(config, "grid")
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

        extension = os.path.splitext(source)[1].lower()
        if extension == ".jpeg":
            extension = ".jpg"
        if extension not in IMAGE_EXTENSIONS:
            print("  %s: %s is not a png or jpg" % (name, source))
            continue

        stem = template % appid
        target = os.path.join(grid, stem + extension)
        print("  %s -> %s" % (source, target))
        if dry_run:
            continue
        os.makedirs(grid, exist_ok=True)
        shutil.copyfile(source, target)
        for other in IMAGE_EXTENSIONS:
            stale = os.path.join(grid, stem + other)
            if stale != target and os.path.exists(stale):
                os.remove(stale)
        wrote = True

    if not wrote and not dry_run:
        print("  nothing to do")
    return wrote


def report_artwork(files):
    for name, _, description in ARTWORK:
        if name in files:
            print("  using %s for %s" % (os.path.basename(files[name]), description))
    missing = [name for name, _, _ in ARTWORK if name not in files]
    if missing:
        print("  nothing named %s, so %s left alone"
              % (" or ".join(missing), "they are" if len(missing) > 1 else "it is"))
    if not files:
        print("  save a png or jpg under one of those names in:")
        for folder in image_folders():
            print("    %s" % folder)


def menu(configs, overrides, dry_run):
    changed = False
    while True:
        print()
        print("Plex, Steam shortcut")
        if steam_is_running():
            print("Steam is running. Restart it afterwards, then check with 4.")
        print()
        print("  1) Add Plex to Steam, with any artwork in this folder")
        print("  2) Remove Plex from Steam")
        print("  3) Add artwork only, from the images in this folder")
        print("  4) Show what is in Steam's shortcut file")
        print("  q) Quit")
        print()

        try:
            choice = input("Choice: ").strip().lower()
        except EOFError:
            choice = "q"

        if choice in ("1", "3"):
            files = find_artwork()
            files.update(overrides)
            print()
            report_artwork(files)
            icon = overrides.get("icon") or find_icon()
            for config in configs:
                if choice == "1":
                    changed |= do_add(config, icon, dry_run, False)
                if files:
                    changed |= do_artwork(config, files, dry_run)
        elif choice == "2":
            for config in configs:
                changed |= do_remove(config, dry_run)
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
    parser.add_argument("--add", action="store_true",
                        help="add the shortcut")
    parser.add_argument("--remove", action="store_true",
                        help="remove the shortcut")
    parser.add_argument("--list", action="store_true",
                        help="show what is in Steam's shortcut file")
    parser.add_argument("--artwork", action="store_true",
                        help="use the portrait, capsule, hero and logo images in this folder")
    parser.add_argument("--icon", help="an image file for the small icon beside the name")
    for name, _, description in ARTWORK:
        parser.add_argument("--" + name, help=description)
    parser.add_argument("-f", "--force", action="store_true",
                        help="add Plex again even if it is already in Steam")
    parser.add_argument("-n", "--dry-run", action="store_true",
                        help="say what would happen and write nothing")
    parser.add_argument("--config", action="append",
                        help="the Steam account folder to write to, if the "
                             "script cannot find it; give it more than once "
                             "for more than one account")
    args = parser.parse_args()

    configs = args.config or find_configs()
    if not configs:
        print("No Steam userdata config directory found.", file=sys.stderr)
        print("Pass one with --config, or sign in to Steam once first.", file=sys.stderr)
        return 1
    missing = [config for config in configs if not os.path.isdir(config)]
    if missing:
        for config in missing:
            print("%s: not a directory" % config, file=sys.stderr)
        return 1

    overrides = {name: getattr(args, name) for name, _, _ in ARTWORK}
    overrides = {name: path for name, path in overrides.items() if path}
    if args.icon:
        overrides["icon"] = args.icon

    batch = args.add or args.remove or args.list or args.artwork or bool(overrides)
    if not batch:
        return menu(configs, overrides, args.dry_run)

    files = {name: path for name, path in overrides.items() if name != "icon"}
    if args.add or args.artwork:
        found = find_artwork()
        found.update(files)
        files = found
        print()
        report_artwork(files)

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
            icon = overrides.get("icon") or find_icon()
            changed |= do_add(config, icon, args.dry_run, args.force)
        elif args.icon and not args.remove:
            changed |= do_icon(config, args.icon, args.dry_run)
        if files:
            changed |= do_artwork(config, files, args.dry_run)

    if changed:
        print("Restart Steam to pick up the changes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
