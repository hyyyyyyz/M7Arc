#!/usr/bin/env python3
"""Exercise Windows SFX payloads and independently deployed Mac extractor bundles."""

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import tempfile


def run(*args, **kwargs):
    return subprocess.run([str(value) for value in args], check=True, capture_output=True, **kwargs)


@contextmanager
def mount_dmg(path):
    mounted = plistlib.loads(run("hdiutil", "attach", "-readonly", "-nobrowse", "-plist", path).stdout)
    volumes = [Path(item["mount-point"]) for item in mounted["system-entities"] if "mount-point" in item]
    assert len(volumes) == 1
    try:
        yield volumes[0]
    finally:
        run("hdiutil", "detach", volumes[0])


@contextmanager
def source_application(args):
    if args.source_dmg:
        with mount_dmg(args.source_dmg) as volume:
            yield volume / "M7Arc.app"
    else:
        yield args.source_app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--windows-stubs", type=Path, required=True)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--source-app", type=Path)
    source.add_argument("--source-dmg", type=Path)
    args = parser.parse_args()
    with source_application(args) as source_app, tempfile.TemporaryDirectory(prefix="m7arc-distribution-") as directory:
        root = Path(directory)
        source = root / "Source"
        source.mkdir()
        contents = ("独立自解压 / M7Arc\n" * 10000).encode()
        (source / "中文 sample.txt").write_bytes(contents)
        (source / "empty").mkdir()
        payload = root / "Payload.7z"
        run(args.cli, "a", "-t7z", "-m0=LZMA2", "-psecret", "-mhe=on", payload, ".", cwd=source)
        for name in ("7z.sfx", "7zCon.sfx"):
            stub = args.windows_stubs / name
            output = root / (name + ".exe")
            run(args.builder, "windows", stub, payload, output)
            data = output.read_bytes()
            header = stub.read_bytes()
            assert data[:len(header)] == header
            assert data[len(header):] == payload.read_bytes()
            assert data[:2] == b"MZ"
            offset = struct.unpack_from("<I", data, 60)[0]
            assert data[offset:offset + 4] == b"PE\0\0"
            assert struct.unpack_from("<H", data, offset + 4)[0] == 0x14C
            destination = root / (name + "-extracted")
            run(args.cli, "x", "-psecret", "-y", f"-o{destination}", output)
            assert (destination / "中文 sample.txt").read_bytes() == contents
            assert (destination / "empty").is_dir()
        print("Windows x86 GUI and console SFX: PE structure, unchanged stub, encrypted payload, actual 7-Zip extraction passed.")
        installer_payload = root / "Installer.7z"
        run(args.cli, "a", "-t7z", "-m0=LZMA2", installer_payload, ".", cwd=source)
        installer_stub = args.windows_stubs / "7zSD.sfx"
        for mode in ("run", "open"):
            output = root / f"installer-{mode}.exe"
            run(args.builder, f"installer-{mode}", installer_stub, installer_payload, output)
            data = output.read_bytes()
            stub = installer_stub.read_bytes()
            assert data[:len(stub)] == stub
            assert data[len(stub):].startswith(b";!@Install@!UTF-8!\n")
            assert data.endswith(installer_payload.read_bytes())
            destination = root / f"installer-{mode}-extracted"
            run(args.cli, "x", "-y", f"-o{destination}", output)
            assert (destination / "中文 sample.txt").read_bytes() == contents
        print("Windows installer SFX: upstream config parser round trip and payload extraction passed; configured commands were not run.")
        if not source_app:
            return
        environment = os.environ.copy()
        for name in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
            environment.pop(name, None)
        password = root / "password.txt"
        password.write_text("secret\n")
        cancelled_app = root / "Cancelled.app"
        cancelled = subprocess.run([str(args.builder), "app", str(source_app), str(payload), str(cancelled_app), "cancel"],
                                   capture_output=True)
        assert cancelled.returncode != 0 and not cancelled_app.exists()
        for mode in ("embedded", "external"):
            app = root / f"独立 {mode}.app"
            run(args.builder, "app", source_app, payload, app, mode)
            run("codesign", "--verify", "--deep", "--strict", app)
            executable = app / "Contents/MacOS/M7ArcExtractor"
            run(executable, "--smoke-test", env=environment, cwd=root)
            destination = root / (mode + "-extracted")
            run(executable, "--extract-to", destination, "--password-file", password, env=environment, cwd=root)
            assert (destination / "中文 sample.txt").read_bytes() == contents
            assert (destination / "empty").is_dir()
            dmg = root / (mode + ".dmg")
            run(args.builder, "dmg", app, "" if mode == "embedded" else payload, dmg)
            with mount_dmg(dmg) as volume:
                mounted_executable = volume / app.name / "Contents/MacOS/M7ArcExtractor"
                destination = root / (mode + "-dmg-extracted")
                run(mounted_executable, "--extract-to", destination, "--password-file", password, env=environment, cwd=root)
                assert (destination / "中文 sample.txt").read_bytes() == contents
        print("Mac embedded/external extractors: independent startup, strict signing, encrypted extraction and mounted DMG extraction passed.")


if __name__ == "__main__":
    main()
