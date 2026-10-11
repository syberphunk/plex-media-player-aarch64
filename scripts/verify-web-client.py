#!/usr/bin/env python3
"""usage: verify-web-client.py <web-client-dir>"""

import base64
import glob
import hashlib
import os
import re
import sys

TAG_RE = re.compile(r"<[a-zA-Z][^>]*>")
INTEGRITY_RE = re.compile(r'integrity="([^"]*)"')
SOURCE_RE = re.compile(r'(?:src|href)="([^"]*)"')

WEBCHANNEL_SRC = "qrc://:/qtwebchannel/qwebchannel.js"
WEBCHANNEL_SRC_QT6 = "qrc:///qtwebchannel/qwebchannel.js"


def subresource_integrity(path, algorithm):
    digest = hashlib.new(algorithm)
    with open(path, "rb") as fp:
        for block in iter(lambda: fp.read(1 << 20), b""):
            digest.update(block)
    return "%s-%s" % (algorithm, base64.b64encode(digest.digest()).decode("ascii"))


def verify_page(page):
    flavour = os.path.dirname(page)
    label = os.path.basename(flavour) or page
    checked = 0

    with open(page, encoding="utf-8") as fp:
        markup = fp.read()

    for tag in TAG_RE.findall(markup):
        declared = INTEGRITY_RE.findall(tag)
        if not declared:
            continue

        source = SOURCE_RE.search(tag)
        if not source:
            sys.exit("%s: integrity attribute on a tag with no src or href: %s"
                     % (label, tag))

        reference = source.group(1)
        if "://" in reference or reference.startswith("//"):
            sys.exit("%s: %s is not a local reference" % (label, reference))

        target = os.path.join(flavour, reference.lstrip("/"))
        if not os.path.isfile(target):
            sys.exit("%s: %s is referenced but missing" % (label, reference))

        for value in declared:
            algorithm = value.split("-", 1)[0]
            if algorithm not in hashlib.algorithms_available:
                sys.exit("%s: unsupported integrity algorithm %r"
                         % (label, algorithm))
            expected = subresource_integrity(target, algorithm)
            if value != expected:
                sys.exit("%s: %s integrity is %s, expected %s"
                         % (label, reference, value, expected))
            checked += 1

    if not checked:
        sys.exit("%s: no integrity attribute to verify" % label)

    return checked


def fix_webchannel_url(page):
    with open(page, encoding="utf-8") as fp:
        markup = fp.read()

    count = markup.count(WEBCHANNEL_SRC)
    if not count:
        if WEBCHANNEL_SRC_QT6 not in markup:
            sys.exit("%s: no qwebchannel.js reference to rewrite" % page)
        return 0

    markup = markup.replace(WEBCHANNEL_SRC, WEBCHANNEL_SRC_QT6)
    with open(page, "w", encoding="utf-8") as fp:
        fp.write(markup)
    return count


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)

    root = argv[1]
    if not os.path.isdir(root):
        sys.exit("%s: not a directory" % root)

    pages = sorted(glob.glob(os.path.join(root, "*", "index.html")))
    if not pages:
        sys.exit("%s: no */index.html found" % root)

    for page in pages:
        rel = os.path.relpath(page, root)
        checked = verify_page(page)
        print("%s: verified %d subresource%s"
              % (rel, checked, "" if checked == 1 else "s"))
        count = fix_webchannel_url(page)
        print("%s: %s" % (rel, "web channel URL rewritten for Qt 6"
                          if count else "web channel URL already rewritten"))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
