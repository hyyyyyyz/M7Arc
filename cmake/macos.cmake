enable_language(OBJCXX)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

file(READ "${CMAKE_CURRENT_LIST_DIR}/../src/platform/services/Services.plist.fragment" M7ARC_SERVICES_PLIST)
find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(M7ARC_PREVIEW_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../src/platform/quicklook")
set(M7ARC_PREVIEW_DATA "${CMAKE_BINARY_DIR}/generated/quicklook")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${M7ARC_PREVIEW_DIRECTORY}/PreviewTypes.py" "${CMAKE_CURRENT_LIST_DIR}/../resources/formats.json")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${M7ARC_PREVIEW_DIRECTORY}/PreviewTypes.py"
    --formats "${CMAKE_CURRENT_LIST_DIR}/../resources/formats.json" --output "${M7ARC_PREVIEW_DATA}"
    COMMAND_ERROR_IS_FATAL ANY)
file(READ "${M7ARC_PREVIEW_DATA}/types.xml" M7ARC_QUICKLOOK_TYPES)
file(READ "${M7ARC_PREVIEW_DATA}/imported-types.xml" M7ARC_IMPORTED_ARCHIVE_TYPES_PLIST)

add_executable(m7arc_quicklook MACOSX_BUNDLE "${M7ARC_PREVIEW_DIRECTORY}/PreviewController.mm")
set_target_properties(m7arc_quicklook PROPERTIES
    OUTPUT_NAME M7ArcPreview BUNDLE_EXTENSION appex AUTOMOC OFF
    MACOSX_BUNDLE_INFO_PLIST "${M7ARC_PREVIEW_DIRECTORY}/Info.plist.in"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../../../../Frameworks")
target_compile_options(m7arc_quicklook PRIVATE -fobjc-arc -fapplication-extension)
target_link_options(m7arc_quicklook PRIVATE "-Wl,-e,_NSExtensionMain")
target_link_libraries(m7arc_quicklook PRIVATE m7arc_engine "-framework AppKit" "-framework QuickLookUI")

function(m7arc_add_macos_integrations application_target)
    set(macos_sources "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform")
    set(macos_tests "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/platform")
    target_sources(${application_target} PRIVATE "${macos_sources}/services/MacServices.mm")
    target_sources(${application_target} PRIVATE "${macos_sources}/drag/MacFilePromises.mm")
    set_source_files_properties("${macos_sources}/services/MacServices.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties("${macos_sources}/drag/MacFilePromises.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(${application_target} PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
    add_dependencies(${application_target} m7arc_quicklook)
    set_property(TARGET ${application_target} APPEND PROPERTY LINK_DEPENDS "$<TARGET_FILE:m7arc_quicklook>")
    foreach(language IN ITEMS en zh-Hans)
        set(strings "${macos_sources}/services/${language}.lproj/ServicesMenu.strings")
        target_sources(${application_target} PRIVATE "${strings}")
        set_source_files_properties("${strings}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/${language}.lproj")
    endforeach()
    add_custom_command(TARGET ${application_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/quick-actions"
            "$<TARGET_BUNDLE_CONTENT_DIR:${application_target}>/Resources/Quick Actions"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${application_target}>/PlugIns"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:m7arc_quicklook>"
            "$<TARGET_BUNDLE_CONTENT_DIR:${application_target}>/PlugIns/M7ArcPreview.appex"
        VERBATIM)
    if(BUILD_TESTING)
        add_executable(m7arc_services_tests
            "${macos_tests}/ServicesTest.mm"
            "${macos_sources}/services/MacServices.mm")
        target_include_directories(m7arc_services_tests PRIVATE "${macos_sources}/..")
        target_compile_options(m7arc_services_tests PRIVATE -fobjc-arc)
        target_link_libraries(m7arc_services_tests PRIVATE Qt6::Core "-framework AppKit" "-framework UniformTypeIdentifiers")
        add_test(NAME platform.services COMMAND m7arc_services_tests)
        add_executable(m7arc_file_promises_tests
            "${macos_tests}/FilePromisesTest.cpp"
            "${macos_sources}/drag/MacFilePromises.mm")
        target_include_directories(m7arc_file_promises_tests PRIVATE "${macos_sources}/..")
        target_compile_options(m7arc_file_promises_tests PRIVATE -fobjc-arc)
        target_link_libraries(m7arc_file_promises_tests PRIVATE m7arc_engine Qt6::Widgets
            "-framework AppKit" "-framework UniformTypeIdentifiers")
        add_test(NAME platform.file_promises COMMAND m7arc_file_promises_tests)
        add_executable(m7arc_preview_tests
            "${macos_tests}/PreviewTest.mm"
            "${macos_sources}/quicklook/PreviewController.mm")
        target_compile_options(m7arc_preview_tests PRIVATE -fobjc-arc)
        target_link_libraries(m7arc_preview_tests PRIVATE m7arc_engine "-framework AppKit" "-framework QuickLookUI")
        add_dependencies(m7arc_preview_tests m7arc_cli)
        add_test(NAME platform.quicklook_controller COMMAND "${Python3_EXECUTABLE}"
            "${macos_tests}/PreviewIntegration.py" "${M7ARC_CLI_PATH}" "$<TARGET_FILE:m7arc_preview_tests>")
        set_tests_properties(platform.quicklook_controller PROPERTIES TIMEOUT 60)
        add_executable(m7arc_workflow_tests "${macos_tests}/WorkflowTest.mm")
        target_compile_options(m7arc_workflow_tests PRIVATE -fobjc-arc)
        target_link_libraries(m7arc_workflow_tests PRIVATE "-framework Automator" "-framework Foundation")
        set(actions "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../resources/quick-actions")
        add_test(NAME platform.quick_actions COMMAND m7arc_workflow_tests
            "${actions}/M7Arc Compress.workflow/Contents/document.wflow"
            "${actions}/M7Arc Extract.workflow/Contents/document.wflow"
            "${actions}/M7Arc Test.workflow/Contents/document.wflow")
    endif()
endfunction()
