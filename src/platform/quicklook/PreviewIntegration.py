#!/usr/bin/env python3
"""Exercise the native preview controller with a real archive."""

from pathlib import Path
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="m7arc-preview-") as directory:
    work = Path(directory)
    source = work / "文档.txt"
    source.write_text("Archive preview contents\n")
    archive = work / "preview.7z"
    subprocess.run([sys.argv[1], "a", str(archive), str(source)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.argv[2], str(archive)], check=True)
