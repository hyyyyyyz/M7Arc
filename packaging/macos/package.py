#!/usr/bin/env python3
"""Deploy a self-contained, ad-hoc signed M7Arc application and disk image."""

import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile


PROJECT = Path(__file__).resolve().parents[2]
PACKAGING = Path(__file__).resolve().parent


def run(*args, capture=False, **kwargs):
    return subprocess.run(
        [str(arg) for arg in args], check=True, text=True,
        stdout=subprocess.PIPE if capture else None, **kwargs
    ).stdout


def read_version():
    version = (PROJECT / "VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(alpha|beta|rc)\.[1-9]\d*)?", version):
        raise ValueError("VERSION must contain X.Y.Z or X.Y.Z-{alpha,beta,rc}.N")
    return version


def mach_objects(app):
    magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf",
             b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
             b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    result = []
    for path in app.rglob("*"):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as stream:
                if stream.read(4) in magic:
                    result.append(path)
    return result


def verify_runtime(app, architecture):
    required = {"arm64", "x86_64"} if architecture == "universal2" else {architecture}
    objects = mach_objects(app)
    for path in objects:
        found = set(run("lipo", "-archs", path, capture=True).split())
        if not required.issubset(found):
            raise RuntimeError(f"{path.relative_to(app)} is missing architectures {required - found}")
        for line in run("otool", "-L", path, capture=True).splitlines():
            if not line.startswith("\t"):
                continue
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
                raise RuntimeError(f"External runtime dependency in {path.name}: {dependency}")
        load_commands = run("otool", "-l", path, capture=True)
        for command in re.split(r"^Load command \d+\s*$", load_commands, flags=re.M):
            if re.search(r"^\s*cmd LC_VERSION_MIN_MACOSX\s*$", command, re.M):
                pattern = r"^\s*version (\d+\.\d+(?:\.\d+)?)\s*$"
            elif re.search(r"^\s*cmd LC_BUILD_VERSION\s*$", command, re.M):
                pattern = r"^\s*minos (\d+\.\d+(?:\.\d+)?)\s*$"
            else:
                continue
            value = re.search(pattern, command, re.M)
            if value and tuple(map(int, value.group(1).split(".")[:2])) > (13, 0):
                raise RuntimeError(f"{path.relative_to(app)} requires macOS {value.group(1)}, above the macOS 13 target")
    return objects


def add_licenses(app):
    licenses = app / "Contents/Resources/Licenses"
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PROJECT / "LICENSE", licenses / "M7Arc.txt")
    seven_zip = licenses / "7-Zip"
    seven_zip.mkdir(exist_ok=True)
    for name in ("License.txt", "copying.txt", "unRarLicense.txt"):
        shutil.copy2(PROJECT / "third_party/7zip/DOC" / name, seven_zip / name)
    shutil.copy2(PROJECT / "third_party/7zip.UPSTREAM.md", seven_zip / "UPSTREAM.md")
    shutil.copytree(PACKAGING / "licenses/Qt", licenses / "Qt", dirs_exist_ok=True)
    for name in ("INSTALL.zh-CN.txt", "INSTALL.en.txt"):
        shutil.copy2(PACKAGING / name, app / "Contents/Resources" / name)
    return licenses


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=PROJECT / "build/release/M7Arc.app")
    parser.add_argument("--qt-prefix", type=Path, required=True, help="Qt installation containing bin/macdeployqt")
    parser.add_argument("--output", type=Path, default=PROJECT / "build/dist")
    args = parser.parse_args()
    version = read_version()
    app = args.app.resolve()
    qt_prefix = args.qt_prefix.resolve()
    output = args.output.resolve()
    macdeployqt = qt_prefix / "bin/macdeployqt"
    for executable in ("M7Arc", "M7ArcExtractor", "7zz", "7zCon.sfx"):
        if not (app / "Contents/MacOS" / executable).is_file():
            parser.error(f"Required executable {executable} is missing from {app}; build the complete application before packaging")
    if not macdeployqt.is_file():
        parser.error(f"macdeployqt was not found at {macdeployqt}")
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    if info.get("CFBundleShortVersionString") != version.split("-", 1)[0]:
        raise RuntimeError("Application bundle version differs from VERSION; rebuild before packaging")
    if info.get("M7ArcVersion", version) != version:
        raise RuntimeError("Application preview version differs from VERSION; rebuild before packaging")
    architectures = set(run("lipo", "-archs", app / "Contents/MacOS/M7Arc", capture=True).split())
    if architectures == {"arm64", "x86_64"}:
        architecture = "universal2"
    elif architectures in ({"arm64"}, {"x86_64"}):
        architecture = next(iter(architectures))
    else:
        raise RuntimeError(f"Unsupported application architectures: {architectures}")
    output.mkdir(parents=True, exist_ok=True)
    dmg = output / f"M7Arc-{version}-macos-{architecture}.dmg"
    with tempfile.TemporaryDirectory(prefix="m7arc-package-", dir=output) as directory, ExitStack() as cleanup:
        staging = Path(directory) / "image"
        staging.mkdir()
        deployed = staging / "M7Arc.app"
        # Launching the smoke test registers this temporary application with LaunchServices.
        cleanup.callback(subprocess.run, [
            "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister",
            "-u", str(deployed),
        ], check=False)
        run("ditto", app, deployed)
        licenses = add_licenses(deployed)
        extractor = deployed / "Contents/MacOS/M7ArcExtractor"
        run(macdeployqt, deployed, "-always-overwrite", "-no-codesign", "-verbose=1", f"-executable={extractor}")
        objects = verify_runtime(deployed, architecture)
        main_executable = deployed / "Contents/MacOS" / info["CFBundleExecutable"]
        for path in sorted(objects, key=lambda item: len(item.parts), reverse=True):
            if path != main_executable:
                run("codesign", "--force", "--sign", "-", "--timestamp=none", path)
        for framework in sorted(deployed.rglob("*.framework"), key=lambda item: len(item.parts), reverse=True):
            run("codesign", "--force", "--sign", "-", "--timestamp=none", framework)
        for extension in sorted(deployed.rglob("*.appex"), key=lambda item: len(item.parts), reverse=True):
            run("codesign", "--force", "--sign", "-", "--timestamp=none", "--entitlements",
                PROJECT / "src/platform/quicklook/Preview.entitlements", extension)
        run("codesign", "--force", "--sign", "-", "--timestamp=none", deployed)
        run("codesign", "--verify", "--deep", "--strict", deployed)
        environment = os.environ.copy()
        for variable in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
            environment.pop(variable, None)
        run(deployed / "Contents/MacOS/M7Arc", "--smoke-test", env=environment, timeout=30, cwd=staging)
        run(extractor, "--smoke-test", env=environment, timeout=30, cwd=staging)
        (staging / "Applications").symlink_to("/Applications", target_is_directory=True)
        shutil.copytree(licenses, staging / "Licenses")
        for name in ("INSTALL.zh-CN.txt", "INSTALL.en.txt"):
            shutil.copy2(PACKAGING / name, staging / name)
        shutil.copy2(PROJECT / "CHANGELOG.md", staging / "CHANGELOG.md")
        run("hdiutil", "create", "-volname", f"M7Arc {version}", "-srcfolder", staging,
            "-format", "UDZO", "-fs", "HFS+", "-ov", dmg)
    print(f"Packaged {dmg}\nSigning: ad-hoc. Developer ID: no. Notarization: no.")


if __name__ == "__main__":
    main()
