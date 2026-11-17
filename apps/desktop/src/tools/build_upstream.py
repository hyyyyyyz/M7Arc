#!/usr/bin/env python3
"""Build an official 7-Zip command-line bundle outside its source tree."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--bundle", choices=("Alone2", "SFXCon"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--arch", nargs="+", choices=("arm64", "x86_64"), required=True)
    parser.add_argument("--deployment-target", default="13.0")
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    program = "7zz" if args.bundle == "Alone2" else "7zCon"
    binaries = []
    for architecture in args.arch:
        objects = output.parent / "objects" / f"{args.bundle}-{architecture}"
        objects.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="m7arc-make-") as directory:
            alias = Path(directory) / "objects"
            alias.symlink_to(objects, target_is_directory=True)
            platform = "arm64" if architecture == "arm64" else "x64"
            command = [
                "/usr/bin/make", "-B", "-f", f"../../cmpl_mac_{platform}.mak",
                f"O={alias}", f"MY_ARCH=-arch {architecture} -mmacosx-version-min={args.deployment_target}",
                f"-j{min(4, os.cpu_count() or 1)}",
            ]
            subprocess.run(command, cwd=source / "CPP/7zip/Bundles" / args.bundle, check=True)
        binaries.append(objects / program)
    if len(binaries) == 1:
        shutil.copy2(binaries[0], output)
    else:
        subprocess.run(["lipo", "-create", *map(str, binaries), "-output", str(output)], check=True)
    output.chmod(0o755)
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(output)], check=True)


if __name__ == "__main__":
    main()
