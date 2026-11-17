fn main() {
    // Lets this package's tests and binaries load an engine located through
    // M7ARC_ENGINE_LIB_DIR, whose install name is `@rpath/libm7arc_engine.dylib`.
    if let Ok(dir) = std::env::var("DEP_M7ARC_ENGINE_LIBRARY_DIR") {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
    }
}
