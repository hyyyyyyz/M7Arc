//! Locates or builds the `m7arc_engine` shared library and links it.
//!
//! * `M7ARC_ENGINE_LIB_DIR` – directory that already contains
//!   `libm7arc_engine.dylib`; nothing is built.
//! * otherwise the engine is built with CMake from `crates/m7arc-sys/engine`
//!   into `M7ARC_ENGINE_BUILD_DIR` (default `<workspace>/build/engine-rust`).
//!   The build is incremental, so repeated cargo runs only pay for changes.

use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn run(command: &mut Command) {
    let status = command
        .status()
        .unwrap_or_else(|e| panic!("cannot run {:?}: {e}", command.get_program()));
    if !status.success() {
        panic!("{:?} failed with {status}", command.get_program());
    }
}

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let workspace = manifest.join("../..").canonicalize().unwrap();
    println!("cargo:rerun-if-env-changed=M7ARC_ENGINE_LIB_DIR");
    println!("cargo:rerun-if-env-changed=M7ARC_ENGINE_BUILD_DIR");

    let library_dir = match env::var_os("M7ARC_ENGINE_LIB_DIR") {
        Some(dir) => PathBuf::from(dir),
        None => build_engine(&manifest, &workspace),
    };
    println!("cargo:rustc-link-search=native={}", library_dir.display());
    println!("cargo:rustc-link-lib=dylib=m7arc_engine");
    println!("cargo:rustc-link-arg=-Wl,-rpath,{}", library_dir.display());
    println!("cargo:library_dir={}", library_dir.display());
}

fn build_engine(manifest: &Path, workspace: &Path) -> PathBuf {
    for watched in ["src/engine", "cmake", "crates/m7arc-sys/engine"] {
        println!("cargo:rerun-if-changed={}", workspace.join(watched).display());
    }
    let build = env::var_os("M7ARC_ENGINE_BUILD_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| workspace.join("build/engine-rust"));
    let mut configure = Command::new("cmake");
    configure
        .arg("-S")
        .arg(manifest.join("engine"))
        .arg("-B")
        .arg(&build)
        .arg("-DCMAKE_BUILD_TYPE=Release");
    let has_ninja = Command::new("ninja").arg("--version").output().is_ok();
    if has_ninja && !build.join("CMakeCache.txt").exists() {
        configure.args(["-G", "Ninja"]);
    }
    run(&mut configure);
    run(Command::new("cmake")
        .arg("--build")
        .arg(&build)
        .args(["--target", "m7arc_engine", "--parallel"]));
    build.join("lib")
}
