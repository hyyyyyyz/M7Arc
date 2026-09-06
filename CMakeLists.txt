cmake_minimum_required(VERSION 3.24)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()

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


include(cmake/macos.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(cmake/engine.cmake)
include(cmake/cli.cmake)

add_library(m7arc_platform STATIC src/platform/MacMetadata.cpp)
target_include_directories(m7arc_platform PUBLIC src)
set_target_properties(m7arc_platform PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Core Gui Widgets Concurrent LinguistTools)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

file(GLOB M7ARC_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp")
qt_add_executable(M7Arc MACOSX_BUNDLE
    src/main.cpp
    ${M7ARC_UI_SOURCES}
    resources/m7arc.qrc
    resources/icons/M7Arc.icns)
set_source_files_properties(resources/icons/M7Arc.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_include_directories(M7Arc PRIVATE src)
target_compile_definitions(M7Arc PRIVATE M7ARC_VERSION="${M7ARC_VERSION}")
target_link_libraries(M7Arc PRIVATE m7arc_engine m7arc_platform Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
m7arc_attach_tools(M7Arc)
m7arc_add_macos_integrations(M7Arc)
qt_add_translations(
    TARGETS M7Arc M7ArcExtractor
    SOURCE_TARGETS M7Arc M7ArcExtractor
    TS_FILES resources/i18n/m7arc_en.ts
    RESOURCE_PREFIX "/i18n"
    NO_GENERATE_PLURALS_TS_FILE)

qt_add_lrelease(
    LRELEASE_TARGET m7arc_qt_translations
    TS_FILES third_party/qttranslations/qtbase_zh_CN.ts
    QM_FILES_OUTPUT_VARIABLE M7ARC_QT_TRANSLATIONS)
foreach(M7ARC_TRANSLATION_TARGET IN ITEMS M7Arc M7ArcExtractor)
    qt_add_resources(${M7ARC_TRANSLATION_TARGET} qt_translations
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${M7ARC_QT_TRANSLATIONS})
endforeach()


file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/formats.json" M7ARC_FORMAT_CATALOG)
string(JSON M7ARC_FORMAT_COUNT LENGTH "${M7ARC_FORMAT_CATALOG}" formats)
math(EXPR M7ARC_LAST_FORMAT "${M7ARC_FORMAT_COUNT} - 1")
set(M7ARC_EXTENSIONS)
foreach(M7ARC_INDEX RANGE 0 ${M7ARC_LAST_FORMAT})
    string(JSON M7ARC_EXTENSION_COUNT LENGTH "${M7ARC_FORMAT_CATALOG}" formats ${M7ARC_INDEX} extensions)
    if(M7ARC_EXTENSION_COUNT GREATER 0)
        math(EXPR M7ARC_LAST_EXTENSION "${M7ARC_EXTENSION_COUNT} - 1")
        foreach(M7ARC_EXTENSION_INDEX RANGE 0 ${M7ARC_LAST_EXTENSION})
            string(JSON M7ARC_EXTENSION GET "${M7ARC_FORMAT_CATALOG}" formats ${M7ARC_INDEX} extensions ${M7ARC_EXTENSION_INDEX})
            if(M7ARC_EXTENSION MATCHES "^[A-Za-z0-9][A-Za-z0-9.]*$")
                list(APPEND M7ARC_EXTENSIONS "${M7ARC_EXTENSION}")
            endif()
        endforeach()
    endif()
endforeach()
list(REMOVE_DUPLICATES M7ARC_EXTENSIONS)
set(M7ARC_DOCUMENT_EXTENSIONS "")
foreach(M7ARC_EXTENSION IN LISTS M7ARC_EXTENSIONS)
    string(APPEND M7ARC_DOCUMENT_EXTENSIONS "<string>${M7ARC_EXTENSION}</string>")
endforeach()

set_target_properties(M7Arc PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "M7Arc"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.m7arc.M7Arc"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${M7ARC_SHORT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${M7ARC_BUNDLE_VERSION}"
    BUILD_WITH_INSTALL_RPATH FALSE
    BUILD_RPATH "@executable_path/../Frameworks"
    INSTALL_RPATH "@executable_path/../Frameworks")

if(APPLE)
    add_custom_command(TARGET M7Arc POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:M7Arc>/Frameworks"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:m7arc_engine>" "$<TARGET_BUNDLE_CONTENT_DIR:M7Arc>/Frameworks/"
        VERBATIM)
endif()

if(BUILD_TESTING)
    add_executable(m7arc_edit_session_tests tests/app/EditSessionTest.cpp src/app/EditSession.cpp)
    target_link_libraries(m7arc_edit_session_tests PRIVATE m7arc_engine Qt6::Core)
    add_test(NAME app.edit_session COMMAND m7arc_edit_session_tests)
    add_executable(m7arc_metadata_tests tests/platform/MacMetadataTest.cpp)
    target_link_libraries(m7arc_metadata_tests PRIVATE m7arc_platform)
    add_test(NAME platform.metadata COMMAND m7arc_metadata_tests)
    add_executable(m7arc_metadata_interop_tests tests/platform/MetadataInterop.cpp)
    target_link_libraries(m7arc_metadata_interop_tests PRIVATE m7arc_engine)
    add_test(NAME platform.metadata_interop COMMAND m7arc_metadata_interop_tests)
    add_executable(m7arc_password_retry_tests tests/app/PasswordRetryTest.cpp src/ui/OperationController.cpp)
    target_link_libraries(m7arc_password_retry_tests PRIVATE m7arc_engine Qt6::Widgets Qt6::Concurrent)
    add_test(NAME app.password_retry COMMAND m7arc_password_retry_tests)
    set_tests_properties(app.password_retry PROPERTIES TIMEOUT 30)
    add_executable(m7arc_create_dialog_tests tests/app/CreateDialogTest.cpp src/ui/ArchiveDialogs.cpp src/app/FormatCatalog.cpp
        src/platform/services/MacServices.mm resources/m7arc.qrc)
    target_link_libraries(m7arc_create_dialog_tests PRIVATE m7arc_engine Qt6::Widgets
        "-framework AppKit" "-framework UniformTypeIdentifiers")
    add_test(NAME app.create_options COMMAND m7arc_create_dialog_tests)
    add_test(NAME app.smoke COMMAND M7Arc --smoke-test)
    get_filename_component(M7ARC_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    set_tests_properties(app.smoke app.password_retry app.create_options PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=${M7ARC_QT_PREFIX}/plugins/platforms")
endif()

install(TARGETS M7Arc BUNDLE DESTINATION . RUNTIME DESTINATION bin)
install(TARGETS m7arc_engine LIBRARY DESTINATION M7Arc.app/Contents/Frameworks)
