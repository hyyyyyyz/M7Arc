# Contributing

Build instructions are in [docs/BUILDING.md](docs/BUILDING.md). Product behavior and the migration scope are documented in [DESIGN.zh-CN.md](DESIGN.zh-CN.md) and the feature specifications under `docs/`.

Keep archive algorithms in the bundled upstream source. Application logic belongs in `src/`; platform operations belong in `src/platform`. Changes to bundled 7-Zip files should be narrow and documented alongside the pinned upstream source information.

Use a separate commit for a coherent change. Explain the user-visible result and how it was checked. For archive operations, exercise a real input that would expose a wrong option, member selection, output path or data loss. Visual adjustments usually need a direct UI check.

Run the checks relevant to your change before opening a pull request. Include the macOS version and architecture when reporting platform-specific behavior. Small archive samples are useful for compatibility reports; remove private data before sharing them.

Use the version rules in [docs/VERSIONING.zh-CN.md](docs/VERSIONING.zh-CN.md). Ordinary changes do not require a version bump; a release commit updates `VERSION` and `CHANGELOG.md` together.

Original contributions use the repository's MIT license. Bundled upstream sources and third-party components retain their own licenses and attribution.
