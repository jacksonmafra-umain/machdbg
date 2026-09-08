include_guard(DIRECTORY)

# https://www.kdab.com/wp-content/uploads/stories/QTVTC20-Using-Modern-CMake-Kevin-Funk.pdf
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
#set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_GLOBAL_AUTOGEN_TARGET ON)
set_property(GLOBAL PROPERTY AUTOGEN_SOURCE_GROUP "Qt")
set_property(GLOBAL PROPERTY AUTOGEN_TARGETS_FOLDER "Qt")
set_property(GLOBAL PROPERTY AUTOMOC_SOURCE_GROUP "Qt")
set_property(GLOBAL PROPERTY AUTOMOC_TARGETS_FOLDER "Qt")
set_property(GLOBAL PROPERTY AUTORCC_SOURCE_GROUP "Qt")
set_property(GLOBAL PROPERTY AUTOUIC_SOURCE_GROUP "Qt")

# Find whether we are using Qt5 or Qt6
# NOTE: do not add components here, it doesn't work
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
set(QT_PACKAGE "Qt${QT_VERSION_MAJOR}")

# Find the exact Qt version
set(QT_LIBRARIES
    ${QT_PACKAGE}::Widgets
    ${QT_PACKAGE}::Svg
    ${QT_PACKAGE}::WebSockets
)
if("${QT_PACKAGE}" STREQUAL "Qt6")
    set(ADDITIONAL_COMPONENTS OpenGLWidgets)
    list(APPEND QT_LIBRARIES Qt6::OpenGLWidgets)
else()
    set(ADDITIONAL_COMPONENTS "")
endif()
find_package(${QT_PACKAGE} COMPONENTS Widgets Svg PrintSupport WebSockets ${ADDITIONAL_COMPONENTS} REQUIRED)
message(STATUS "Found ${QT_PACKAGE}: ${${QT_PACKAGE}_DIR}")

# https://stackoverflow.com/a/41199492/1806760
# TODO: set VCINSTALLDIR environment variable
# TODO: move to a custom target you can trigger manually
# TODO: support macdeployqt
if(${QT_PACKAGE}_FOUND AND WIN32 AND TARGET ${QT_PACKAGE}::qmake AND NOT TARGET ${QT_PACKAGE}::windeployqt)
    get_target_property(_qt_qmake_location ${QT_PACKAGE}::qmake IMPORTED_LOCATION)

    execute_process(
        COMMAND "${_qt_qmake_location}" -query QT_INSTALL_PREFIX
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE qt_install_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(imported_location "${qt_install_prefix}/bin/windeployqt.exe")

    if(EXISTS ${imported_location})
        add_executable(${QT_PACKAGE}::windeployqt IMPORTED)

        set_target_properties(${QT_PACKAGE}::windeployqt PROPERTIES
            IMPORTED_LOCATION ${imported_location}
        )
    endif()
endif()

function(qt_executable tgt)
    if("${QT_PACKAGE}" STREQUAL "Qt6")
        if(APPLE)
            qt_add_executable(${tgt} MACOSX_BUNDLE ${ARGN})
        else()
            qt_add_executable(${tgt} WIN32 ${ARGN})
        endif()
    else()
        add_executable(${tgt} ${ARGN})
    endif()
    target_link_libraries(${tgt} PRIVATE ${QT_LIBRARIES})

    if(APPLE)
        set_target_properties(${tgt} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/../../packaging/Info.plist.in"
            MACOSX_BUNDLE_BUNDLE_NAME "${tgt}"
            MACOSX_BUNDLE_EXECUTABLE_NAME "${tgt}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.machdbg.${tgt}"
            MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        )

        # CMAKE_CURRENT_LIST_DIR here is the directory of the listfile that CALLS
        # qt_executable() (src/cross, since qt_executable is a function: CMake
        # resolves CMAKE_CURRENT_LIST_DIR against the call site, not the file
        # that defines the function), not the directory containing Qt.cmake
        # itself. Verified with a debug message() during configure. Two levels
        # up from src/cross reaches the repository root, matching the sibling
        # MACOSX_BUNDLE_INFO_PLIST path above (which spells the same directory
        # via CMAKE_SOURCE_DIR).
        set(_icns "${CMAKE_CURRENT_LIST_DIR}/../../packaging/machdbg.icns")
        target_sources(${tgt} PRIVATE "${_icns}")
        set_source_files_properties("${_icns}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        set_target_properties(${tgt} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "machdbg.icns"
        )
    endif()

    # Run windeployqt after build to copy Qt DLLs next to the executable.
    # Only do this once per runtime output directory to avoid multiple targets
    # racing to deploy the same Qt files into the same location.
    if(WIN32 AND TARGET ${QT_PACKAGE}::windeployqt)
        get_target_property(_qt_runtime_output_dir ${tgt} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT _qt_runtime_output_dir)
            set(_qt_runtime_output_dir "${CMAKE_CURRENT_BINARY_DIR}")
        endif()

        get_property(_qt_deployed_dirs GLOBAL PROPERTY X64DBG_QT_DEPLOYED_DIRS)
        if(NOT _qt_deployed_dirs)
            set(_qt_deployed_dirs "")
        endif()

        list(FIND _qt_deployed_dirs "${_qt_runtime_output_dir}" _qt_deploy_dir_index)
        if(_qt_deploy_dir_index EQUAL -1)
            add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND ${QT_PACKAGE}::windeployqt
                    --force
                    --no-translations
                    --no-compiler-runtime
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    "$<TARGET_FILE:${tgt}>"
                COMMENT "Running windeployqt on ${tgt}..."
            )
            set_property(GLOBAL APPEND PROPERTY X64DBG_QT_DEPLOYED_DIRS "${_qt_runtime_output_dir}")
        endif()
    endif()
endfunction()
