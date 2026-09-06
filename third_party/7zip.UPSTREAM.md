# 7-Zip upstream

| Field | Value |
| --- | --- |
| Repository | https://github.com/ip7z/7zip |
| Website | https://www.7-zip.org/ |
| Version | 26.03 |
| Commit | `0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17` |
| Source release date | 2026-09-03, from `C/7zVersion.h` |
| Snapshot directory | `7zip/` |
| Source files | 1,294, excluding Git repository metadata |
| Local modifications | None |

The snapshot is tracked as ordinary files in the M7Arc repository. It includes the complete official source tree and original license documents; it has no nested `.git` directory and requires no submodule initialization.

The source licenses are described in [7zip/DOC/License.txt](7zip/DOC/License.txt), with full LGPL text in [copying.txt](7zip/DOC/copying.txt) and the unRAR terms in [unRarLicense.txt](7zip/DOC/unRarLicense.txt).

To update the snapshot, obtain a specific upstream release, replace the unmodified upstream files, update this version record, and reconcile the explicit CMake source list and any documented platform patches. Verify the resulting format and method registrations against the matching official `7zz` build, then exercise the affected archive and platform operations.
