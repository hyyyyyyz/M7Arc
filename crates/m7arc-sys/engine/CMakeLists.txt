# Engine-only build used by m7arc-sys. It builds m7arc_engine (and, with
# -DBUILD_TESTING=ON, the engine tests) without the Qt application targets.
cmake_minimum_required(VERSION 3.24)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()
project(M7ArcEngine LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(M7ARC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
include(CTest)
include("${M7ARC_ROOT}/cmake/engine.cmake")
add_library(m7arc_platform STATIC "${M7ARC_ROOT}/src/platform/MacMetadata.cpp")
target_include_directories(m7arc_platform PUBLIC "${M7ARC_ROOT}/src")
set_target_properties(m7arc_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Binaries linked against this build find the library without an rpath.
set_target_properties(m7arc_engine PROPERTIES INSTALL_NAME_DIR "${CMAKE_BINARY_DIR}/lib")
