#!/usr/bin/env python3
"""usage: check-sources.py [options]

  --update             fetch newer upstream releases, rewrite the pins and the
                       README version table
  --deep               hash every remote source instead of only checking it exists
  --fail-on-outdated   exit non-zero when a newer upstream release exists
  --offline            skip every network check
  --manifest PATH      manifest to read (default: the one in this repo)
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "packaging", "flatpak",
                        "io.github.syberphunk.PlexMediaPlayer.yml")
VENDOR = os.path.join(REPO, "vendor-deps")

PLEX_DESKTOP_API = "https://plex.tv/api/downloads/6.json"
PLEX_TARBALL = ("https://downloads.plex.tv/plex-desktop/%s/linux_tarball/"
                "Plex-%s-linux-x86_64.tar.bz2")

BASE_APP_REPO = "flathub/io.qt.qtwebengine.BaseApp"
BASE_APP_COMMIT = "ab059370c5faf2f7579cfad6eebedad167024886"

WORKFLOWS = os.path.join(REPO, ".github", "workflows")

ACTIONS = {
    "actions/checkout": ("v7.0.1", "3d3c42e5aac5ba805825da76410c181273ba90b1"),
    "actions/upload-artifact": ("v7.0.2", "cf430e030ddbb5b0abf93d22962f4752f3646cd9"),
    "actions/attest-build-provenance": ("v4.2.2", "4d101475d8b20a2381f78447822ac1eab6504dd8"),
    "actions/upload-pages-artifact": ("v5.0.0", "fc324d3547104276b827a68afc52ff2a11cc49c9"),
    "actions/deploy-pages": ("v5.0.1", "368f82528645a54fb793d4d04e342629a3f51346"),
}

USES_RE = re.compile(r"^\s*(?:-\s+)?uses:\s*(\S+)\s*$")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")

UPSTREAM = {
    "libass": {
        "probe": ("github", "libass/libass"),
        "strip_v": True,
        "url": "https://github.com/libass/libass/releases/download/%(v)s/libass-%(v)s.tar.gz",
        "vendor": "libass-%(v)s.tar.gz",
        "readme": ("libass", ""),
    },
    "hwdata": {
        "probe": ("github", "vcrhonek/hwdata"),
        "strip_v": True,
        "url": "https://github.com/vcrhonek/hwdata/archive/refs/tags/v%(v)s.tar.gz",
        "vendor": "hwdata-%(v)s.tar.gz",
        "readme": ("hwdata", "v"),
    },
    "libdisplay-info": {
        "probe": ("gitlab", "https://gitlab.freedesktop.org",
                  "emersion%2Flibdisplay-info"),
        "strip_v": True,
        "asset": ("https://gitlab.freedesktop.org", "emersion%2Flibdisplay-info",
                  "libdisplay-info-%(v)s.tar.xz"),
        "vendor": "libdisplay-info-%(v)s.tar.xz",
        "readme": ("libdisplay-info", ""),
    },
    "libmpv": {
        "probe": ("github", "mpv-player/mpv"),
        "strip_v": True,
        "url": "https://github.com/mpv-player/mpv/archive/refs/tags/v%(v)s.tar.gz",
        "vendor": "mpv-%(v)s.tar.gz",
        "readme": ("mpv (libmpv)", "v"),
    },
    "minizip": {
        "probe": ("github", "madler/zlib"),
        "strip_v": True,
        "url": "https://zlib.net/fossils/zlib-%(v)s.tar.gz",
        "fallback_url": "https://zlib.net/zlib-%(v)s.tar.gz",
        "vendor": "zlib-%(v)s.tar.gz",
        "readme": ("zlib (minizip)", ""),
        "hold": ("1.3.1", "CVE-2026-85091 affects 1.3.1.2 through 1.3.2"),
    },
    "libplacebo": {
        "probe": ("gitlab", "https://code.videolan.org", "videolan%2Flibplacebo"),
        "strip_v": True,
        "vendor": "libplacebo-%(v)s.tar.gz",
        "vendor_script": "vendor-libplacebo.sh",
        "readme": ("libplacebo", "v"),
    },
    "plex-media-player": {
        "probe": ("plex", None),
        "strip_v": False,
        "url": None,
        "vendor": None,
        "readme": ("Plex Desktop web client", ""),
    },
}

README = os.path.join(REPO, "README.md")

VERSION_RE = re.compile(r"[0-9]+(?:\.[0-9]+)+")


class Source(object):
    def __init__(self, module):
        self.module = module
        self.fields = {}
        self.lines = {}


def parse_manifest(path):
    with open(path, encoding="utf-8") as fp:
        raw = fp.read().splitlines()

    sources = []
    module = None
    current = None
    in_sources = False

    for number, line in enumerate(raw):
        stripped = line.strip()
        indent = len(line) - len(line.lstrip())

        found = re.match(r"-\s+name:\s*(\S+)", stripped)
        if found and indent == 2:
            module = found.group(1)
            in_sources = False
            current = None
            continue

        if stripped == "sources:" and module:
            in_sources = True
            continue

        if not in_sources:
            continue

        if indent <= 4 and stripped and not stripped.startswith("-"):
            in_sources = False
            current = None
            continue

        if stripped.startswith("- ") and indent == 6:
            current = Source(module)
            sources.append(current)
            stripped = stripped[2:]
            indent = 8

        if current is None:
            continue

        pair = re.match(r"([a-z0-9-]+):\s*(.*)$", stripped)
        if not pair:
            continue
        key, value = pair.group(1), pair.group(2).strip()
        current.fields[key] = value
        current.lines[key] = number

    return raw, sources


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for block in iter(lambda: fp.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def request(url, method="GET"):
    headers = {"User-Agent": "plex-media-player-aarch64-check-sources"}
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    parts = urllib.parse.urlsplit(url)
    if token and parts.scheme == "https" and parts.hostname == "api.github.com":
        headers["Authorization"] = "Bearer " + token
    return urllib.request.Request(url, headers=headers, method=method)


def head_ok(url):
    try:
        with urllib.request.urlopen(request(url, "HEAD"), timeout=30) as reply:
            return reply.status == 200, reply.headers.get("Content-Length")
    except urllib.error.HTTPError as problem:
        if problem.code in (403, 405):
            try:
                with urllib.request.urlopen(request(url), timeout=30) as reply:
                    reply.read(1)
                    return True, reply.headers.get("Content-Length")
            except Exception as retry:
                return False, str(retry)
        return False, "HTTP %d" % problem.code
    except Exception as problem:
        return False, str(problem)


def download(url, dest):
    with urllib.request.urlopen(request(url), timeout=120) as reply:
        with open(dest, "wb") as fp:
            shutil.copyfileobj(reply, fp)
    return sha256_file(dest)


def remote_sha256(url):
    handle, temp = tempfile.mkstemp(prefix="check-sources.")
    os.close(handle)
    try:
        return download(url, temp)
    finally:
        os.unlink(temp)


def fetch_json(url):
    with urllib.request.urlopen(request(url), timeout=30) as reply:
        return json.load(reply)


def base_app_head(branch):
    data = fetch_json("https://api.github.com/repos/%s/commits/branch/%s"
                      % (BASE_APP_REPO, branch))
    return data["sha"], data["commit"]["message"].split("\n")[0]


def check_base_app(branch):
    print()
    print("==> qtwebengine base app")
    try:
        sha, subject = base_app_head(branch)
    except Exception as problem:
        report("base-version %s" % branch, False,
               "commit probe failed: %s" % problem)
        return False, True
    if sha == BASE_APP_COMMIT:
        report("base-version %s" % branch, True,
               "%s is current (%s)" % (sha[:12], subject))
        return False, False
    print("    %-6s %-28s %s -> %s available: %s"
          % ("newer", "base-version %s" % branch,
             BASE_APP_COMMIT[:12], sha[:12], subject))
    return True, False


def workflow_uses():
    found = []
    if not os.path.isdir(WORKFLOWS):
        return found
    for name in sorted(os.listdir(WORKFLOWS)):
        if not name.endswith((".yml", ".yaml")):
            continue
        path = os.path.join(WORKFLOWS, name)
        with open(path, encoding="utf-8") as fp:
            for number, line in enumerate(fp.read().splitlines(), 1):
                match = USES_RE.match(line)
                if match and not match.group(1).startswith("./"):
                    found.append((name, number, match.group(1)))
    return found


def action_latest(action):
    data = fetch_json("https://api.github.com/repos/%s/releases/latest" % action)
    return data["tag_name"]


def check_actions(offline):
    print()
    print("==> workflow actions")

    failures = []
    newer = []
    seen = set()

    for name, number, reference in workflow_uses():
        action, _, pin = reference.partition("@")
        seen.add(action)
        where = "%s:%d" % (name, number)
        if action not in ACTIONS:
            failures.append(report(action, False, "%s is not pinned here" % where))
            continue
        version, sha = ACTIONS[action]
        if not SHA_RE.match(pin):
            failures.append(report(action, False,
                                   "%s uses the mutable ref %s" % (where, pin)))
        elif pin != sha:
            failures.append(report(action, False,
                                   "%s is %s, expected %s (%s)"
                                   % (where, pin[:12], sha[:12], version)))

    for action in sorted(set(ACTIONS) - seen):
        failures.append(report(action, False, "pinned here but used by no workflow"))

    if not failures:
        report("workflow actions", True,
               "%d action(s) pinned by commit" % len(seen))

    if offline:
        return failures, newer

    for action in sorted(seen & set(ACTIONS)):
        version, sha = ACTIONS[action]
        try:
            latest = action_latest(action)
        except Exception as problem:
            failures.append(report(action, False, "release probe failed: %s" % problem))
            continue
        if latest != version:
            newer.append(action)
            print("    %-6s %-28s %s -> %s available"
                  % ("newer", action, version, latest))

    return failures, newer


def latest_version(spec):
    kind = spec[0]
    if kind == "github":
        data = fetch_json("https://api.github.com/repos/%s/releases/latest"
                          % spec[1])
        return data["tag_name"]
    if kind == "gitlab":
        tags = fetch_json("%s/api/v4/projects/%s/repository/tags?per_page=20"
                          % (spec[1], spec[2]))
        for tag in tags:
            name = tag["name"]
            if "-rc" not in name and VERSION_RE.search(name):
                return name
        return None
    if kind == "plex":
        data = fetch_json(PLEX_DESKTOP_API)
        versions = set()
        for platform in data.get("computer", {}).values():
            if platform.get("version"):
                versions.add(platform["version"])
        if len(versions) != 1:
            raise RuntimeError("Plex Desktop reports versions %s"
                               % sorted(versions))
        return versions.pop()
    raise RuntimeError("unknown probe %r" % (spec,))


def release_asset(spec, version):
    host, project, name = spec
    wanted = name % {"v": version}
    releases = fetch_json("%s/api/v4/projects/%s/releases" % (host, project))
    for release in releases:
        for link in release.get("assets", {}).get("links", []):
            if link.get("name") == wanted or link.get("url", "").endswith("/" + wanted):
                return link["url"]
    return None


def pinned_version(source, module):
    for key in ("url", "path", "dest-filename"):
        value = source.fields.get(key)
        if not value:
            continue
        if module == "plex-media-player" and key == "url":
            found = re.search(r"plex-desktop/([^/]+)/", value)
            if found:
                return found.group(1)
        found = VERSION_RE.search(os.path.basename(value))
        if found:
            return found.group(0)
    return None


def normalise(version, strip_v):
    if version is None:
        return None
    return version[1:] if strip_v and version.startswith("v") else version


def interesting(sources):
    out = []
    for source in sources:
        if source.fields.get("type") not in ("archive", "file", "extra-data"):
            continue
        if "sha256" not in source.fields:
            continue
        out.append(source)
    return out


def report(name, ok, detail):
    print("    %-6s %-28s %s" % ("ok" if ok else "FAIL", name, detail))
    return ok


def check(sources, deep, offline):
    failures = []
    outdated = []

    print("==> pinned sources")
    for source in interesting(sources):
        module = source.module
        pin = source.fields["sha256"]
        name = os.path.basename(source.fields.get("path")
                                or source.fields.get("dest-filename")
                                or source.fields.get("url", "?"))

        local = None
        if source.fields.get("path"):
            local = os.path.normpath(os.path.join(os.path.dirname(MANIFEST),
                                                  source.fields["path"]))
            if not os.path.exists(local):
                local = None
                failures.append(report(module, False,
                                       "%s: no in-repo copy" % name))
        else:
            candidate = os.path.join(VENDOR, name)
            if os.path.exists(candidate):
                local = candidate

        if local is None:
            if not source.fields.get("path"):
                report(module, True, "%s: remote only, pinned %s"
                       % (name, pin[:12]))
        else:
            actual = sha256_file(local)
            label = "%s: %s" % (os.path.relpath(local, REPO), actual[:12])
            if actual == pin:
                report(module, True, label + " matches pin")
            else:
                failures.append(report(module, False,
                                       label + " != pinned %s" % pin[:12]))

        if offline:
            continue

        for key in ("url",):
            url = source.fields.get(key)
            if not url:
                continue
            if deep:
                try:
                    actual = remote_sha256(url)
                except Exception as problem:
                    failures.append(report(module, False,
                                           "upstream unreachable: %s" % problem))
                    continue
                if actual == pin:
                    report(module, True, "upstream serves the pinned bytes")
                else:
                    failures.append(report(module, False,
                                           "upstream now serves %s" % actual[:12]))
            else:
                ok, detail = head_ok(url)
                if ok:
                    report(module, True, "upstream reachable")
                else:
                    failures.append(report(module, False,
                                           "upstream unreachable: %s" % detail))
                    continue

                declared = source.fields.get("size")
                if declared and detail and str(detail).isdigit():
                    if declared == str(detail):
                        report(module, True, "upstream serves %s bytes, as pinned"
                               % declared)
                    else:
                        failures.append(report(module, False,
                                               "upstream serves %s bytes, pinned %s"
                                               % (detail, declared)))

    if offline:
        return failures, outdated

    print()
    print("==> upstream versions")
    for source in interesting(sources):
        spec = UPSTREAM.get(source.module)
        if not spec:
            continue
        pin = pinned_version(source, source.module)
        if pin is None:
            continue
        try:
            latest = normalise(latest_version(spec["probe"]), spec["strip_v"])
        except Exception as problem:
            report(source.module, False, "version probe failed: %s" % problem)
            failures.append(False)
            continue
        if latest is None:
            continue
        hold = spec.get("hold")
        if latest == pin:
            report(source.module, True, "%s is current" % pin)
        elif hold and hold[0] == pin:
            report(source.module, True, "%s held, not taking %s: %s"
                   % (pin, latest, hold[1]))
        else:
            print("    %-6s %-28s %s -> %s available"
                  % ("newer", source.module, pin, latest))
            outdated.append((source.module, pin, latest))

    return failures, outdated


def readme_rows(sources, runtime_version):
    wanted = {}
    for source in interesting(sources):
        spec = UPSTREAM.get(source.module)
        if not spec or not spec.get("readme"):
            continue
        pin = pinned_version(source, source.module)
        if pin is None:
            continue
        row, prefix = spec["readme"]
        wanted[row] = prefix + pin
    if runtime_version:
        wanted["org.kde.Platform / org.kde.Sdk"] = runtime_version
        wanted["io.qt.qtwebengine.BaseApp"] = runtime_version
    return wanted


def readme_sync(wanted, rewrite):
    if not os.path.exists(README):
        return []
    with open(README, encoding="utf-8") as fp:
        lines = fp.read().splitlines()

    stale = []
    for number, line in enumerate(lines):
        cells = [cell.strip() for cell in line.split("|")]
        if len(cells) != 5 or not cells[0] == "":
            continue
        row = cells[1]
        if row not in wanted or cells[2] == wanted[row]:
            continue
        stale.append("%s: README says %s, manifest pins %s"
                     % (row, cells[2] or "nothing", wanted[row]))
        cells[2] = wanted[row]
        lines[number] = "| " + " | ".join(cells[1:4]) + " |"

    if stale and rewrite:
        with open(README, "w", encoding="utf-8") as fp:
            fp.write("\n".join(lines) + "\n")
    return stale


def replace_line(raw, number, value):
    line = raw[number]
    indent = line[:len(line) - len(line.lstrip())]
    stripped = line.strip()
    if stripped.startswith("- "):
        return indent + "- " + value
    key = stripped.split(":", 1)[0]
    return "%s%s: %s" % (indent, key, value)


def update(raw, sources, outdated):
    changed = False
    for module, pin, latest in outdated:
        spec = UPSTREAM[module]
        source = None
        for candidate in interesting(sources):
            if candidate.module == module and pinned_version(candidate, module) == pin:
                source = candidate
                break
        if source is None:
            continue

        print()
        print("==> %s %s -> %s" % (module, pin, latest))

        if spec.get("vendor_script"):
            script = os.path.join(REPO, "scripts", spec["vendor_script"])
            tag = latest if latest.startswith("v") else "v" + latest
            vendor_name = spec["vendor"] % {"v": tag}
            target = os.path.join(VENDOR, vendor_name)
            subprocess.check_call([script, tag, target])
            digest = sha256_file(target)
            raw[source.lines["path"]] = replace_line(
                raw, source.lines["path"], "../../vendor-deps/" + vendor_name)
            raw[source.lines["sha256"]] = replace_line(
                raw, source.lines["sha256"], digest)
            old_tag = pin if pin.startswith("v") else "v" + pin
            old = os.path.join(VENDOR, spec["vendor"] % {"v": old_tag})
            if os.path.exists(old) and os.path.abspath(old) != os.path.abspath(target):
                os.unlink(old)
            print("    vendored %s (%s)" % (vendor_name, digest[:12]))
            changed = True
            continue

        if module == "plex-media-player":
            url = PLEX_TARBALL % (latest, latest)
            ok, detail = head_ok(url)
            if not ok:
                print("    %s: %s -- leaving the pin alone" % (url, detail))
                continue
            digest = remote_sha256(url)
            raw[source.lines["url"]] = replace_line(raw, source.lines["url"], url)
            raw[source.lines["sha256"]] = replace_line(
                raw, source.lines["sha256"], digest)
            if "size" in source.lines and detail and str(detail).isdigit():
                raw[source.lines["size"]] = replace_line(
                    raw, source.lines["size"], str(detail))
            print("    pinned %s (%s)" % (os.path.basename(url), digest[:12]))
            print("    the client's own integrity attributes are re-checked on"
                  " the target by verify-web-client.py")
            changed = True
            continue

        if spec.get("asset"):
            try:
                url = release_asset(spec["asset"], latest)
            except Exception as problem:
                print("    release lookup failed: %s -- leaving the pin alone"
                      % problem)
                continue
            if not url:
                print("    no release asset found for %s -- leaving the pin alone"
                      % latest)
                continue
        else:
            url = spec["url"] % {"v": latest}
        ok, _ = head_ok(url)
        if not ok and spec.get("fallback_url"):
            url = spec["fallback_url"] % {"v": latest}
            ok, _ = head_ok(url)
        if not ok:
            print("    no release archive found for %s -- leaving the pin alone"
                  % latest)
            continue

        vendor_name = spec["vendor"] % {"v": latest}
        target = os.path.join(VENDOR, vendor_name)
        digest = download(url, target)
        raw[source.lines["path"]] = replace_line(
            raw, source.lines["path"], "../../vendor-deps/" + vendor_name)
        raw[source.lines["sha256"]] = replace_line(
            raw, source.lines["sha256"], digest)
        old = os.path.join(VENDOR, spec["vendor"] % {"v": pin})
        if os.path.exists(old) and os.path.abspath(old) != os.path.abspath(target):
            os.unlink(old)
        print("    vendored %s (%s)" % (vendor_name, digest[:12]))
        changed = True

    return changed


def main(argv):
    global MANIFEST

    options = set(argv[1:])
    manifest = MANIFEST
    if "--manifest" in options:
        index = argv.index("--manifest")
        manifest = argv[index + 1]
        options.discard("--manifest")
        options.discard(manifest)

    known = {"--update", "--deep", "--fail-on-outdated", "--offline"}
    unknown = options - known
    if unknown:
        sys.exit(__doc__)

    MANIFEST = manifest

    raw, sources = parse_manifest(manifest)
    if not sources:
        sys.exit("%s: no sources found" % manifest)

    offline = "--offline" in options
    failures, outdated = check(sources, "--deep" in options, offline)

    updating = "--update" in options

    if updating and outdated:
        if update(raw, sources, outdated):
            with open(manifest, "w", encoding="utf-8") as fp:
                fp.write("\n".join(raw) + "\n")
            print()
            print("==> rewrote %s" % os.path.relpath(manifest, REPO))
            raw, sources = parse_manifest(manifest)

    runtime = ""
    base_version = ""
    for line in raw:
        found = re.match(r"runtime-version:\s*'?([^'\s]+)'?\s*$", line)
        if found:
            runtime = found.group(1)
        found = re.match(r"base-version:\s*'?([^'\s]+)'?\s*$", line)
        if found:
            base_version = found.group(1)

    base_newer = False
    if not offline and base_version:
        base_newer, probe_failed = check_base_app(base_version)
        if probe_failed:
            failures.append(False)

    action_failures, actions_newer = check_actions(offline)
    failures.extend(action_failures)

    print()
    print("==> README versions")
    stale = readme_sync(readme_rows(sources, runtime), updating)
    for item in stale:
        report("README.md", updating, item)
    if not stale:
        report("README.md", True, "matches the manifest")
    elif updating:
        print("    rewrote README.md")
    else:
        failures.extend([False] * len(stale))

    if updating:
        print()
        print("    re-run without --update to verify the new pins")
        return 0

    print()
    if failures:
        print("%d check(s) failed" % len(failures))
        return 1
    if outdated and "--fail-on-outdated" in options:
        print("%d source(s) outdated" % len(outdated))
        return 2
    trailing = []
    if outdated:
        trailing.append("%d newer upstream release(s) available" % len(outdated))
    if base_newer:
        trailing.append("a newer qtwebengine base app is available")
    if actions_newer:
        trailing.append("%d newer action release(s) available" % len(actions_newer))
    print("all pinned sources verified"
          + (" (%s)" % ", ".join(trailing) if trailing else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
