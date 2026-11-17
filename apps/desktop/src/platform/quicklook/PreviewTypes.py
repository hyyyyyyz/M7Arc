#!/usr/bin/env python3
"""Write archive preview type declarations from the format catalogue."""

import argparse
import json
from pathlib import Path
from xml.sax.saxutils import escape


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--formats", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    known = {"7z": "org.7-zip.7-zip-archive", "zip": "public.zip-archive", "rar": "com.rarlab.rar-archive",
             "rar5": "com.rarlab.rar-archive", "tar": "org.gnu.gnu-tar-archive", "gzip": "org.gnu.gnu-zip-archive",
             "bzip2": "public.bzip2-archive", "xz": "org.tukaani.xz-archive", "dmg": "com.apple.disk-image-udif"}
    excluded = {"img", "doc", "xls", "ppt", "docx", "xlsx", "odt", "ods", "epub", "ipa", "apk"}
    formats = json.loads(args.formats.read_text())["formats"]
    declarations = {}
    for item in formats:
        if item["kind"] not in ("archive", "image", "filesystem", "split") and item["id"] not in ("bzip2", "gzip", "xz", "zstd", "lzma", "lzma86", "ppmd", "z"):
            continue
        extensions = [ext for ext in item["extensions"] if ext not in excluded]
        if not extensions:
            continue
        identifier = known.get(item["id"], "org.m7arc.archive." + item["id"])
        entry = declarations.setdefault(identifier, {"name": item["name"], "extensions": []})
        entry["extensions"] = sorted(set(entry["extensions"] + extensions))
    types = "".join("<string>" + escape(identifier) + "</string>" for identifier in sorted(declarations))
    fragment = "<key>UTImportedTypeDeclarations</key><array>"
    for identifier, entry in sorted(declarations.items()):
        fragment += "<dict><key>UTTypeIdentifier</key><string>" + escape(identifier) + "</string>"
        fragment += "<key>UTTypeDescription</key><string>" + escape(entry["name"]) + " archive</string>"
        fragment += "<key>UTTypeConformsTo</key><array><string>public.archive</string><string>public.data</string></array>"
        fragment += "<key>UTTypeTagSpecification</key><dict><key>public.filename-extension</key><array>"
        fragment += "".join("<string>" + escape(extension) + "</string>" for extension in entry["extensions"])
        fragment += "</array></dict></dict>"
    fragment += "</array>"
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "types.xml").write_text(types)
    (args.output / "imported-types.xml").write_text(fragment)


if __name__ == "__main__":
    main()
