# Windows SFX modules

Version: 7-Zip 26.03 (2026-09-03). All three modules are Windows x86 PE32 executables.

Source: the official [7-Zip download page](https://www.7-zip.org/download.html),
[`7z2603-x64.exe`](https://github.com/ip7z/7zip/releases/download/26.03/7z2603-x64.exe).

Files extracted unchanged from that installer:

- `7z.sfx`: Windows graphical self-extractor.
- `7zCon.sfx`: Windows console self-extractor.
- `License.txt`: license distributed with the official installer.

The corresponding source is in this repository's `third_party/7zip/CPP/7zip/Bundles/SFXWin`
and `SFXCon`, with supporting code in the same 7-Zip source tree.
The installer and all unrelated Windows components are excluded from this directory.

The additional installer module is extracted unchanged from the official
[`lzma2603.7z` LZMA SDK](https://github.com/ip7z/7zip/releases/download/26.03/lzma2603.7z):

- `7zSD.sfx`: `bin/7zSD.sfx`, the installer module using the Windows `msvcrt.dll` runtime.
- `LZMA-SDK.txt`: original `DOC/lzma-sdk.txt`, including the public domain declaration.
- `installer.txt`: original `DOC/installer.txt`, including the supported configuration syntax.

The SDK permits copying and distributing its compiled binaries and joining installer
modules with other software. The corresponding source is in
`third_party/7zip/CPP/7zip/Bundles/SFXSetup` and `CPP/Common/TextConfig.cpp`.
The `7zSD.sfx` decoder set supports LZMA, LZMA2, Copy and associated branch/Delta filters;
it does not include AES or PPMd. M7Arc's installer target uses this decoder set.

M7Arc appends a 7z payload to the selected unmodified module. The output is a Windows
x86 executable and is not macOS executable code. The Mac tests validate its PE structure,
archive offset, contents, and extraction using 7-Zip. Windows runtime validation is separate.
Installer configuration is also parsed with the same upstream `GetTextConfig` implementation
to verify UTF-8, quotes, backslashes, line breaks, launch parameters and directory prefixes.
The configured Windows command is not executed during package creation or Mac verification.
