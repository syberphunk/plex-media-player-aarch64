#!/usr/bin/env python3
"""usage: release-notes.py <version> [metainfo.xml]"""

import re
import sys
import textwrap
import xml.etree.ElementTree as ET
from pathlib import Path

APP_ID = "io.github.syberphunk.PlexMediaPlayer"
DEFAULT_METAINFO = (
    Path(__file__).resolve().parent.parent
    / "packaging"
    / "flatpak"
    / f"{APP_ID}.metainfo.xml"
)
WIDTH = 78


def inline(element):
    marks = {"code": "`", "em": "*"}
    out = [element.text or ""]
    for child in element:
        mark = marks.get(child.tag, "")
        out.append(f"{mark}{inline(child)}{mark}")
        out.append(child.tail or "")
    return "".join(out)


def flatten(text):
    return re.sub(r"\s+", " ", text).strip()


def render(release):
    description = release.find("description")
    if description is None:
        return []

    lines = []
    for node in description:
        if node.tag == "p":
            heading = flatten(inline(node)).rstrip(":")
            if lines:
                lines.append("")
            lines.append(f"### {heading}")
            lines.append("")
        elif node.tag in ("ul", "ol"):
            for item in node.findall("li"):
                lines.extend(
                    textwrap.wrap(
                        flatten(inline(item)),
                        width=WIDTH,
                        initial_indent="- ",
                        subsequent_indent="  ",
                    )
                )
    return lines


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        sys.exit(__doc__.strip())

    version = argv[1].lstrip("v")
    path = Path(argv[2]) if len(argv) > 2 else DEFAULT_METAINFO

    root = ET.parse(path).getroot()
    for release in root.iterfind("./releases/release"):
        if release.get("version") == version:
            lines = render(release)
            if not lines:
                sys.exit(f"{path}: release {version} has no description")
            print("\n".join(lines))
            return
    sys.exit(f"{path}: no release entry for {version}")


if __name__ == "__main__":
    main(sys.argv)
