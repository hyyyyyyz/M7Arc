cmake_minimum_required(VERSION 3.24)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" M7ARC_VERSION LIMIT_COUNT 1)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" M7ARC_SHORT_VERSION "${M7ARC_VERSION}")
project(M7Arc VERSION "${M7ARC_SHORT_VERSION}" LANGUAGES C CXX)
set(M7ARC_BUNDLE_VERSION "${M7ARC_SHORT_VERSION}")
if(M7ARC_VERSION MATCHES "-(alpha|beta|rc)\\.([0-9]+)$")
    set(M7ARC_STAGE "${CMAKE_MATCH_1}")
    set(M7ARC_STAGE_NUMBER "${CMAKE_MATCH_2}")
    if(M7ARC_STAGE STREQUAL "alpha")
        set(M7ARC_STAGE "a")
    elseif(M7ARC_STAGE STREQUAL "beta")
        set(M7ARC_STAGE "b")
    else()
        set(M7ARC_STAGE "fc")
    endif()
    string(APPEND M7ARC_BUNDLE_VERSION "${M7ARC_STAGE}${M7ARC_STAGE_NUMBER}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
# Executables and application bundles stay at the top of the build tree whichever directory defines them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
include(cmake/engine.cmake)

# Shared by the engine and the applications.
add_library(m7arc_platform STATIC src/platform/MacMetadata.cpp)
target_include_directories(m7arc_platform PUBLIC src)
set_target_properties(m7arc_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_subdirectory(apps/cli)
add_subdirectory(apps/desktop)
