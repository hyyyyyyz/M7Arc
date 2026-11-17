# Rust crates

| Crate | Purpose |
|---|---|
| `m7arc-sys` | Raw bindings to the engine C interface (`src/engine/capi/m7arc_c.h`). Its `build.rs` locates or builds `libm7arc_engine.dylib`. |
| `m7arc-core` | Safe API: `Engine`, typed `Error`, option structs, closure callbacks, `CancelToken`. |

Both crates have no crates.io dependencies.

## Building and testing

Requirements: macOS 13+, Rust 1.80+, CMake 3.24+ (Ninja is used when installed) and the Xcode command line tools. Qt is not needed.

```sh
cargo build            # builds the engine with CMake on first use, then the crates
cargo test --workspace # round-trip tests: create, list, test, extract, edit, passwords, cancellation, hashing
```

`build.rs` runs CMake on `crates/m7arc-sys/engine` (an engine-only project that includes `cmake/engine.cmake`) and writes the build tree to `build/engine-rust`; later runs are incremental. The first engine build compiles the vendored 7-Zip sources (about 22 s on a 10-core machine).

| Variable | Effect |
|---|---|
| `M7ARC_ENGINE_BUILD_DIR` | Build tree for the engine (default `<repo>/build/engine-rust`). |
| `M7ARC_ENGINE_LIB_DIR` | Use an existing directory containing `libm7arc_engine.dylib` (for example the app bundle's `Frameworks`) and skip the CMake build. Binaries that link `m7arc-core` get an rpath to it. |

The cargo target directory is `build/cargo` (see `.cargo/config.toml`).

## Using `m7arc-core`

```rust
use m7arc_core::{Callbacks, CreateOptions, Engine, OpenOptions};

let engine = Engine::new()?;
engine.create(&["/abs/path/folder"], "/abs/out.7z", &CreateOptions::default(), &mut Callbacks::new())?;
let archive = engine.list("/abs/out.7z", &OpenOptions::default(), &mut Callbacks::new())?;
```

Operations that run but report member failures return `Error::OperationFailed(report)`; `Error::Cancelled`, `PasswordRequired` and `PasswordOrCorruption` are distinct variants. `Engine` is `Send` but not `Sync`; callbacks must be `Send`.

## C interface tests

`ctest` runs `engine.capi` (a plain C program against `m7arc_c.h`). To run only the engine tests without Qt:

```sh
cmake -S crates/m7arc-sys/engine -B build/engine-rust -DBUILD_TESTING=ON
cmake --build build/engine-rust
ctest --test-dir build/engine-rust
```
