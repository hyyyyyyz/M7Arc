find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

set(M7ARC_TOOLS_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../third_party/7zip")
set(M7ARC_TOOLS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../src/tools/build_upstream.py")
set(M7ARC_TOOLS_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
set(M7ARC_CLI_PATH "${M7ARC_TOOLS_DIRECTORY}/7zz")
set(M7ARC_SFX_PATH "${M7ARC_TOOLS_DIRECTORY}/7zCon.sfx")

if(CMAKE_OSX_ARCHITECTURES)
    set(M7ARC_TOOL_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
else()
    set(M7ARC_TOOL_ARCHITECTURES ${CMAKE_SYSTEM_PROCESSOR})
endif()
if(NOT APPLE)
    message(FATAL_ERROR "The bundled CLI and SFX build requires macOS")
endif()

file(GLOB_RECURSE M7ARC_TOOL_DEPENDENCIES CONFIGURE_DEPENDS
    "${M7ARC_TOOLS_SOURCE}/C/*.c" "${M7ARC_TOOLS_SOURCE}/C/*.h"
    "${M7ARC_TOOLS_SOURCE}/CPP/*.cpp" "${M7ARC_TOOLS_SOURCE}/CPP/*.h"
    "${M7ARC_TOOLS_SOURCE}/CPP/*.mak" "${M7ARC_TOOLS_SOURCE}/CPP/*/makefile.gcc"
    "${M7ARC_TOOLS_SOURCE}/Asm/*.S" "${M7ARC_TOOLS_SOURCE}/Asm/*.asm")

foreach(M7ARC_TOOL IN ITEMS cli sfx)
    if(M7ARC_TOOL STREQUAL "cli")
        set(M7ARC_TOOL_BUNDLE Alone2)
        set(M7ARC_TOOL_OUTPUT "${M7ARC_CLI_PATH}")
    else()
        set(M7ARC_TOOL_BUNDLE SFXCon)
        set(M7ARC_TOOL_OUTPUT "${M7ARC_SFX_PATH}")
    endif()
    add_custom_command(
        OUTPUT "${M7ARC_TOOL_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}" "${M7ARC_TOOLS_SCRIPT}"
            --source "${M7ARC_TOOLS_SOURCE}" --bundle ${M7ARC_TOOL_BUNDLE}
            --output "${M7ARC_TOOL_OUTPUT}" --arch ${M7ARC_TOOL_ARCHITECTURES}
            --deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
        DEPENDS "${M7ARC_TOOLS_SCRIPT}" ${M7ARC_TOOL_DEPENDENCIES}
        VERBATIM
        COMMENT "Building official 7-Zip ${M7ARC_TOOL_BUNDLE}")
    add_custom_target(m7arc_${M7ARC_TOOL} DEPENDS "${M7ARC_TOOL_OUTPUT}")
endforeach()

# Test targets and CTest entries for the tools built above.
function(m7arc_define_tool_tests)
    add_executable(m7arc_sfx_builder_tests
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/tools/SfxBuilderTest.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/SfxBuilder.cpp")
    target_include_directories(m7arc_sfx_builder_tests PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    target_link_libraries(m7arc_sfx_builder_tests PRIVATE Qt6::Core)
    add_dependencies(m7arc_sfx_builder_tests m7arc_cli m7arc_sfx)
    add_test(NAME tools.sfx_builder COMMAND m7arc_sfx_builder_tests "${M7ARC_CLI_PATH}" "${M7ARC_SFX_PATH}")
    set_tests_properties(tools.sfx_builder PROPERTIES TIMEOUT 90)
    add_executable(m7arc_distribution_builder_tests
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/tools/DistributionBuilderTest.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/DistributionBuilder.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/SfxBuilder.cpp"
        "${M7ARC_TOOLS_SOURCE}/CPP/Common/TextConfig.cpp")
    target_include_directories(m7arc_distribution_builder_tests PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src"
        "${M7ARC_TOOLS_SOURCE}/CPP")
    target_link_libraries(m7arc_distribution_builder_tests PRIVATE Qt6::Core m7arc_engine)
    add_dependencies(m7arc_distribution_builder_tests m7arc_cli)
    add_test(NAME tools.windows_sfx COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/tools/DistributionIntegration.py"
        --builder "$<TARGET_FILE:m7arc_distribution_builder_tests>" --cli "${M7ARC_CLI_PATH}"
        --windows-stubs "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/windows" --part windows)
    set_tests_properties(tools.windows_sfx PROPERTIES TIMEOUT 90)
    # Needs a deployed M7Arc.app: pass -DM7ARC_SFX_SOURCE_APP=<path>. Without it CTest reports the test as skipped.
    set(M7ARC_SFX_SOURCE_APP "" CACHE PATH "Deployed M7Arc.app used by tools.mac_sfx_bundle")
    set(mac_bundle_arguments --part mac)
    if(M7ARC_SFX_SOURCE_APP)
        list(APPEND mac_bundle_arguments --source-app "${M7ARC_SFX_SOURCE_APP}")
    endif()
    add_test(NAME tools.mac_sfx_bundle COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/tools/DistributionIntegration.py"
        --builder "$<TARGET_FILE:m7arc_distribution_builder_tests>" --cli "${M7ARC_CLI_PATH}"
        --windows-stubs "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/windows" ${mac_bundle_arguments})
    set_tests_properties(tools.mac_sfx_bundle PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
    add_test(NAME tools.integration
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/tools/ToolsIntegration.py"
        --cli "${M7ARC_CLI_PATH}" --sfx "${M7ARC_SFX_PATH}"
        --builder "$<TARGET_FILE:m7arc_distribution_builder_tests>")
    set_tests_properties(tools.integration PROPERTIES TIMEOUT 90)
endfunction()

function(m7arc_attach_tools application_target)
    target_sources(${application_target} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/SfxBuilder.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/DistributionBuilder.cpp")
    add_executable(M7ArcExtractor
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/extractor/main.cpp")
    target_include_directories(M7ArcExtractor PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    target_compile_definitions(M7ArcExtractor PRIVATE M7ARC_VERSION="${M7ARC_VERSION}")
    target_link_libraries(M7ArcExtractor PRIVATE m7arc_ui m7arc_app m7arc_engine Qt6::Widgets)
    set_target_properties(M7ArcExtractor PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_target(m7arc_bundle_tools
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:${application_target}>/Contents/MacOS"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${M7ARC_CLI_PATH}" "$<TARGET_BUNDLE_DIR:${application_target}>/Contents/MacOS/7zz"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${M7ARC_SFX_PATH}" "$<TARGET_BUNDLE_DIR:${application_target}>/Contents/MacOS/7zCon.sfx"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:M7ArcExtractor>"
            "$<TARGET_BUNDLE_DIR:${application_target}>/Contents/MacOS/M7ArcExtractor"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/tools/windows"
            "$<TARGET_BUNDLE_DIR:${application_target}>/Contents/Resources/Windows SFX"
        DEPENDS m7arc_cli m7arc_sfx M7ArcExtractor
        VERBATIM)
    add_dependencies(${application_target} m7arc_bundle_tools)
    if(BUILD_TESTING)
        m7arc_define_tool_tests()
    endif()
endfunction()
