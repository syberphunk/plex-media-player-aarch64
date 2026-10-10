#!/usr/bin/env python3
"""usage: stage-web-client.py <plex-desktop-tree> <dest>"""

import os
import re
import shutil
import sys

VERSION_RE = re.compile(r"-plex-([0-9][^-]*)-")

def find_client(root):
    if os.path.isfile(os.path.join(root, "index.html")):
        return root
    for base, dirs, _ in os.walk(root):
        if os.path.basename(base) == "web-client" and \
                os.path.isfile(os.path.join(base, "index.html")):
            return base
        dirs[:] = [d for d in dirs if not d.startswith(".")]
    return None

def client_version(client):
    versions = set()
    for name in os.listdir(client):
        found = VERSION_RE.search(name)
        if found:
            versions.add(found.group(1))
    if len(versions) != 1:
        sys.exit("%s: expected one client version, found %s"
                 % (client, sorted(versions) or "none"))
    return versions.pop()

def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__)

    root, dest = argv[1], argv[2]
    client = find_client(root)
    if client is None:
        sys.exit("%s: no resources/web-client/index.html below here" % root)

    version = client_version(client)
    sha_path = os.path.join(client, "current_sha.txt")
    sha = "unknown"
    if os.path.isfile(sha_path):
        with open(sha_path, encoding="utf-8") as fp:
            sha = fp.read().strip() or sha

    for flavour in ("desktop", "tv"):
        target = os.path.join(dest, flavour)
        shutil.rmtree(target, ignore_errors=True)
        shutil.copytree(client, target)

    with open(os.path.join(dest, "buildid.cmake"), "w", encoding="utf-8") as fp:
        fp.write('set(WEB_CLIENT_VERSION_DESKTOP "%s")\n' % version)
        fp.write('set(WEB_CLIENT_VERSION_TV "%s")\n' % version)
        fp.write('set(WEB_CLIENT_BUILD_ID "%s")\n' % sha)

    print("staged Plex Web %s (%s) to %s" % (version, sha, dest))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
