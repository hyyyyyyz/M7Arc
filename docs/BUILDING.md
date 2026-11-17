# Building M7Arc

M7Arc is a C++20 and Qt Widgets application for macOS 13 or newer. The complete 7-Zip 26.03 source snapshot is included in `third_party/7zip`; no adjacent checkout is needed.

## Requirements

- Apple command line developer tools and a macOS SDK
- CMake 3.24 or newer and Ninja
- Qt 6.11.2 for macOS, including `qtbase`, `qttools` and the `qtdeclarative` dependency of the Linguist tools
- Python 3.9 or newer for the packaging, test and upstream build scripts (CI uses 3.13)
- Rust 1.80 or newer (`cargo`) to build the `m7arc` command line and include it in the app. Without cargo, configuration prints a warning and the app is built without `m7arc`; packaging then stops with a message that it is missing.

The official Qt `clang_64` SDK contains both arm64 and x86_64 libraries. The SDK directory name is `macos`.

```sh
xcode-select --install
brew install cmake ninja
python3 -m venv .cache/build-tools
.cache/build-tools/bin/python -m pip install aqtinstall==3.3.0
.cache/build-tools/bin/aqt install-qt mac desktop 6.11.2 clang_64 \
  --outputdir .qt --archives qtbase qttools qtdeclarative
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
open build/release/M7Arc.app
```

The `release` preset builds for the host architecture. The `arm64` and `x86_64` presets select one architecture explicitly; `debug` enables a Debug build. Running an x86_64 executable on Apple Silicon requires Rosetta. CI configures, builds and tests through these presets, running each architecture on a matching GitHub-hosted runner.

For an existing Qt installation, pass its prefix when configuring:

```sh
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.2/macos
```

## Build products

- `M7Arc.app`: the Qt file manager
- `lib/libm7arc_engine.dylib`: the in-process engine with all built-in formats
- `M7Arc.app/Contents/Helpers/m7arc`: the Rust command line ([apps/cli](../apps/cli/README.md)); it loads the engine from `Contents/Frameworks`. `build/<preset>/m7arc` is the same binary outside the bundle
- `M7Arc.app/Contents/MacOS/7zz`: the official full command line application
- `M7Arc.app/Contents/MacOS/7zCon.sfx`: the console self-extractor module

The engine compiles an explicit source list into an object library. All registration objects are included in the shared library. The GUI uses structured C++ calls; it does not parse CLI output to perform archive operations.

## Checks

CTest covers the format/method registry, the C interface, archive round trips and editing, selected extraction, passwords, cancellation, macOS metadata, CLI registration and console SFX extraction. The app smoke test checks Qt startup and engine loading. These checks use temporary sample files and do not modify user archives.

The graphical self-extractor test `tools.mac_sfx_bundle` needs a deployed `M7Arc.app`. Configure with `-DM7ARC_SFX_SOURCE_APP=<path to M7Arc.app>` to run it; otherwise CTest lists it as skipped. CI runs it against the packaged installation image. Tests for `apps/desktop/src/tools` live in `apps/desktop/tests/tools`, tests for `apps/desktop/src/platform` and `src/platform/MacMetadata.cpp` in `apps/desktop/tests/platform`, and the engine tests in `tests/engine`. The Rust crates have their own tests: `cargo test --workspace` from the repository root.

Changes to `third_party/7zip` are kept as patch files in `third_party/patches`; see the [upstream patch guide](UPSTREAM_PATCHES.zh-CN.md) before updating the snapshot.

## Creating an installation image

```sh
python3 packaging/macos/package.py --qt-prefix .qt/6.11.2/macos
```

The script stages the app, deploys Qt, embeds third-party licenses, checks runtime dependencies (including `m7arc`), applies ad-hoc signatures and starts the relocated `M7Arc`, `M7ArcExtractor` and `m7arc` before producing a DMG in `build/dist`. The build directory remains available for development.

Published preview packages do not use Developer ID signing or Apple notarization. Installation steps and release commands are in [the release guide](RELEASE.zh-CN.md). Application versioning is defined in [VERSIONING.zh-CN.md](VERSIONING.zh-CN.md).

## Rebuilding and replacing libraries

The app dynamically links Qt and the 7-Zip engine. Their corresponding source references and licenses are included in the repository and installation image. Build a compatible replacement with the same CPU architecture and deployment target, copy it into the app's `Contents/Frameworks` directory, then re-apply local ad-hoc signatures to the modified components and app. The project does not require a vendor certificate to run a locally rebuilt application.
