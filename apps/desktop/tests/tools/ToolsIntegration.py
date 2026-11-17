#!/usr/bin/env python3
"""Verify the complete CLI registry and console SFX extraction using the application's SFX builder."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def command(*args, cwd=None):
    result = subprocess.run([str(value) for value in args], cwd=cwd, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise AssertionError(f"Command failed ({result.returncode}): {args}\n{result.stdout}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--sfx", type=Path, required=True)
    parser.add_argument("--builder", type=Path, required=True, help="test driver around m7arc::tools::createConsoleSfx")
    args = parser.parse_args()
    cli, stub = args.cli.resolve(), args.sfx.resolve()
    catalog = command(cli, "i")
    sections = {}
    current = None
    for line in catalog.splitlines():
        if line in ("Formats:", "Codecs:", "Hashers:"):
            current = line[:-1]
            sections[current] = []
        elif current and line.strip() and line.startswith(" "):
            sections[current].append(line)
    counts = {name: len(rows) for name, rows in sections.items()}
    if counts != {"Formats": 61, "Codecs": 25, "Hashers": 10}:
        raise AssertionError(f"Official registry differs from the 26.03 baseline: {counts}")
    with tempfile.TemporaryDirectory(prefix="m7arc-sfx-test-") as directory:
        work = Path(directory)
        source = work / "输入 文件"
        source.mkdir()
        (source / "empty").mkdir()
        content = "M7Arc 压缩与自解压验证\n".encode()
        (source / "文档.txt").write_bytes(content)
        (source / "run.sh").write_text("#!/bin/sh\nexit 0\n")
        (source / "run.sh").chmod(0o755)
        archive = work / "payload.7z"
        command(cli, "a", "-t7z", "-m0=LZMA2", "-mx=3", archive, source.name, cwd=work)
        output = work / "M7Arc Archive.command"
        command(args.builder.resolve(), "console", stub, archive, output)
        command("codesign", "--verify", "--strict", output)
        command(output, "t", "-y")
        destination = work / "extracted"
        command(output, "x", "-y", f"-o{destination}")
        extracted = destination / source.name
        if (extracted / "文档.txt").read_bytes() != content:
            raise AssertionError("SFX extracted content differs from its input")
        if not (extracted / "empty").is_dir():
            raise AssertionError("SFX did not restore an empty directory")
        if not (extracted / "run.sh").stat().st_mode & 0o111:
            raise AssertionError("SFX did not preserve executable permissions")
    print(f"CLI registry: {counts}. Console SFX: extraction and metadata passed.")


if __name__ == "__main__":
    main()
