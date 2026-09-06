#!/usr/bin/env python3
"""Write GitHub release notes from VERSION and the matching changelog entry."""

import argparse
from pathlib import Path
import re

from package import PROJECT, read_version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    version = read_version()
    if args.tag != f"v{version}":
        parser.error(f"Tag must match VERSION: v{version}")
    changelog = (PROJECT / "CHANGELOG.md").read_text()
    match = re.search(rf"^## {re.escape(version)}(?:\s+—[^\n]*)?\s*\n(.*?)(?=^## |\Z)", changelog, re.M | re.S)
    if not match or not match.group(1).strip():
        parser.error(f"CHANGELOG.md needs a populated ## {version} entry")
    notes = match.group(1).strip() + "\n\n"
    notes += "## macOS downloads\n\n"
    notes += "- **Apple Silicon:** download the `macos-arm64.dmg` asset.\n"
    notes += "- **Intel:** download the `macos-x86_64.dmg` asset.\n"
    notes += "- Requires macOS 13 or later. Qt and 7-Zip runtime components are included.\n"
    notes += "- Open the DMG, drag M7Arc to Applications, eject the DMG, and launch the app.\n\n"
    notes += "**Signing: ad-hoc. No Developer ID signature. Not notarized.** If macOS blocks the first launch and you trust this download, use System Settings → Privacy & Security → Open Anyway for M7Arc. [Apple instructions](https://support.apple.com/en-us/102445).\n\n"
    notes += "安装：打开 DMG，将 M7Arc 拖入“应用程序”。本发行采用 ad-hoc 签名，没有 Developer ID 签名和 Apple 公证。首次启动被阻止时，确认下载来源后，在“系统设置 → 隐私与安全性”对 M7Arc 选择“仍要打开”。\n\n"
    notes += "The GitHub source archives contain application and vendored 7-Zip sources. They are separate from the installable DMG assets. License files and Qt source information are included in each DMG.\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes)
    print(version)


if __name__ == "__main__":
    main()
