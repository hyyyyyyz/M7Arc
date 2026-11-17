# Builds the m7arc command line with cargo against the engine built by this project, so the
# application bundle and the command line share one libm7arc_engine.dylib.
find_program(M7ARC_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin")
if(NOT M7ARC_CARGO)
    message(WARNING "cargo was not found: the m7arc command line will not be built or bundled. "
        "Install Rust (https://rustup.rs) and reconfigure to include it.")
    return()
endif()

set(M7ARC_CARGO_DIRECTORY "${CMAKE_BINARY_DIR}/cargo")
set(M7ARC_CLI_BINARY "${CMAKE_BINARY_DIR}/m7arc")

set(M7ARC_RUST_TRIPLES)
foreach(M7ARC_ARCHITECTURE IN LISTS CMAKE_OSX_ARCHITECTURES)
    if(M7ARC_ARCHITECTURE STREQUAL "arm64")
        list(APPEND M7ARC_RUST_TRIPLES aarch64-apple-darwin)
    elseif(M7ARC_ARCHITECTURE STREQUAL "x86_64")
        list(APPEND M7ARC_RUST_TRIPLES x86_64-apple-darwin)
    endif()
endforeach()

set(M7ARC_CARGO_COMMANDS)
set(M7ARC_CARGO_OUTPUTS)
if(M7ARC_RUST_TRIPLES)
    foreach(M7ARC_TRIPLE IN LISTS M7ARC_RUST_TRIPLES)
        list(APPEND M7ARC_CARGO_COMMANDS COMMAND "${CMAKE_COMMAND}" -E env
            "M7ARC_ENGINE_LIB_DIR=$<TARGET_FILE_DIR:m7arc_engine>" "CARGO_TARGET_DIR=${M7ARC_CARGO_DIRECTORY}"
            "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "${M7ARC_CARGO}" build --release --locked -p m7arc-cli --target ${M7ARC_TRIPLE}
            --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml")
        list(APPEND M7ARC_CARGO_OUTPUTS "${M7ARC_CARGO_DIRECTORY}/${M7ARC_TRIPLE}/release/m7arc")
    endforeach()
else()
    list(APPEND M7ARC_CARGO_COMMANDS COMMAND "${CMAKE_COMMAND}" -E env
        "M7ARC_ENGINE_LIB_DIR=$<TARGET_FILE_DIR:m7arc_engine>" "CARGO_TARGET_DIR=${M7ARC_CARGO_DIRECTORY}"
        "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "${M7ARC_CARGO}" build --release --locked -p m7arc-cli
        --manifest-path "${PROJECT_SOURCE_DIR}/Cargo.toml")
    list(APPEND M7ARC_CARGO_OUTPUTS "${M7ARC_CARGO_DIRECTORY}/release/m7arc")
endif()
list(LENGTH M7ARC_CARGO_OUTPUTS M7ARC_CARGO_OUTPUT_COUNT)
if(M7ARC_CARGO_OUTPUT_COUNT GREATER 1)
    list(APPEND M7ARC_CARGO_COMMANDS COMMAND lipo -create ${M7ARC_CARGO_OUTPUTS} -output "${M7ARC_CLI_BINARY}")
else()
    list(APPEND M7ARC_CARGO_COMMANDS COMMAND "${CMAKE_COMMAND}" -E copy "${M7ARC_CARGO_OUTPUTS}" "${M7ARC_CLI_BINARY}")
endif()

# cargo decides what is out of date; the target runs on every build and is a no-op when nothing changed.
add_custom_target(m7arc_command_line ALL
    ${M7ARC_CARGO_COMMANDS}
    DEPENDS m7arc_engine
    BYPRODUCTS "${M7ARC_CLI_BINARY}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
    COMMENT "Building the m7arc command line (cargo)")
set(M7ARC_CLI_BINARY "${M7ARC_CLI_BINARY}" CACHE INTERNAL "Path of the built m7arc command line")
