#!/usr/bin/env python3
"""Combine the bundled console extractor with a 7z payload for the same Mac architecture."""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess


def create_sfx(stub, archive, output):
    stub, archive, output = (Path(path).resolve() for path in (stub, archive, output))
    if output in (stub, archive):
        raise ValueError("The output must differ from the stub and payload paths")
    with archive.open("rb") as stream:
        if stream.read(6) != b"7z\xbc\xaf\x27\x1c":
            raise ValueError("The console extractor requires a 7z payload")
    with output.open("xb") as target, stub.open("rb") as source:
        shutil.copyfileobj(source, target)
    try:
        subprocess.run(["codesign", "--remove-signature", str(output)], check=True)
        with output.open("ab") as target, archive.open("rb") as source:
            shutil.copyfileobj(source, target)
        with output.open("r+b") as target:
            header = target.read(32)
            if struct.unpack_from("<I", header)[0] != 0xFEEDFACF:
                raise ValueError("The SFX stub must be a single-architecture 64-bit Mach-O")
            offset = 32
            for _ in range(struct.unpack_from("<I", header, 16)[0]):
                target.seek(offset)
                load = target.read(72)
                command, length = struct.unpack_from("<II", load)
                if command == 0x19 and load[8:24].rstrip(b"\0") == b"__LINKEDIT":
                    file_offset = struct.unpack_from("<Q", load, 40)[0]
                    size = output.stat().st_size - file_offset
                    target.seek(offset + 32)
                    target.write(struct.pack("<Q", (size + 0x3FFF) & ~0x3FFF))
                    target.seek(offset + 48)
                    target.write(struct.pack("<Q", size))
                    break
                offset += length
            else:
                raise ValueError("The SFX stub has no __LINKEDIT segment")
        output.chmod(0o755)
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(output)], check=True)
    except Exception:
        output.unlink()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stub", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    create_sfx(args.stub, args.archive, args.output)
    print(args.output)


if __name__ == "__main__":
    main()
