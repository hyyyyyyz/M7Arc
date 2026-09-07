# Changelog

Application versions follow [Semantic Versioning](docs/VERSIONING.zh-CN.md). The 7-Zip engine has its own version.

## Unreleased

## 0.1.0-alpha.2

### Changed

- Orange Fold 7 app icon with an ivory rounded tile, including all macOS icon sizes and graphical self-extracting applications.
- Editable color and monochrome project marks, light/dark wordmarks, and a shared brand guide.
- Repository header uses the project wordmark and follows the page appearance.

### Fixed

- Incremental builds now pick up changes to VERSION automatically, keeping the main application, extractor and bundle versions consistent.

### Known limitations

- No Developer ID signature or Apple notarization. First launch may require the macOS Privacy & Security exception for this application.
- macOS 13 installation, Intel desktop interaction and Windows SFX execution remain subject to device validation. Archive capabilities and other limitations remain as documented for 0.1.0-alpha.1.

## 0.1.0-alpha.1

First executable preview of the C++20 / Qt Widgets file manager for macOS, using the complete official 7-Zip 26.03 engine.

### Added

- All 61 built-in handlers, 25 codecs and 10 hashers; searchable format, method and external module catalogs.
- Creation and updates for 7z, ZIP, gzip, bzip2, xz, TAR and WIM, plus three compressed TAR presets. Includes encryption, volumes, solid blocks, compression properties, path modes, filters, owner and timestamp options.
- Archive browsing, selected extraction, integrity tests, password retry, overwrite prompts, progress, pause and cancellation. Member deletion, precise renaming, directory creation and ZIP comments follow actual format capabilities.
- Single and dual panes, four views, dynamic properties, favorites, selection masks, file transfers and nested archive editing with explicit save/discard/cancel handling.
- macOS metadata preservation, Finder file promises and clipboard transfers, Services, installable Quick Actions, file associations and Quick Look archive previews.
- Per-file hashes and 7-Zip aggregate digests, selectable algorithms, SHA1/SHA256 manifests and verification; repeated benchmarks with current/average speed, CPU usage and ratings; file splitting, combining and links.
- Official command-line `7zz`, macOS console SFX, standalone graphical extractor apps and DMGs, Windows graphical/console SFX, and Windows installer SFX with explicit launch configuration.
- Native external codec/format modules using the current 7-Zip interfaces, with a buildable example and real cross-module codec tests.
- Chinese and English interfaces, format descriptions and searchable offline help.
- Apple Silicon and Intel build presets, dependency deployment, ad-hoc signing, DMG packaging and tag-triggered GitHub Releases.

### Compatibility

- ZIP metadata interoperability with macOS `ditto` preserves file contents, executable permissions, resource forks, binary extended attributes, FinderInfo, Finder tags and ACLs in both directions.
- Archive updates preserve the original output until replacement succeeds. Archive moves retain the source when extraction is skipped or a destination operation fails.

### Known limitations

- No Developer ID signature or Apple notarization. First launch may require the macOS Privacy & Security exception for this application.
- Deployment targets macOS 13. Validation on the minimum OS and an Intel Mac remains separate from Apple Silicon and Rosetta checks.
- Windows SFX modules and payloads are verified on macOS; running the resulting applications requires Windows. Installer commands are packaged but never executed during creation.
- RAR/RAR5 creation, Zstandard encoding and disk-image writing are outside the built-in upstream capabilities. WIM creation uses Store; archive hard-link records require TAR or WIM.
- Quick Look and Services availability depends on system registration and user extension settings. One file manager window runs one engine operation at a time.
