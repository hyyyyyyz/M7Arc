fn main() {
    // The engine is a shared library found through rpaths: the directory it was
    // linked from (a CMake build tree or M7ARC_ENGINE_LIB_DIR) for development,
    // and the locations used by an installed prefix (`bin/m7arc` next to
    // `lib/`) or an app bundle (`Contents/Helpers/m7arc` next to `Contents/Frameworks/`).
    if let Ok(dir) = std::env::var("DEP_M7ARC_ENGINE_LIBRARY_DIR") {
        println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{dir}");
    }
    for relative in ["@executable_path/../lib", "@executable_path/../Frameworks"] {
        println!("cargo:rustc-link-arg-bins=-Wl,-rpath,{relative}");
    }
}
