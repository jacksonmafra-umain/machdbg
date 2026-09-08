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

# Qt 6's own CMake package config already exports a Qt6::macdeployqt imported target
# (unlike windeployqt above, which genuinely needs the block below to create one), so this
# guard is normally false and the block does not run. It is kept as a fallback for Qt
# versions or configurations where that target is absent.
if(${QT_PACKAGE}_FOUND AND APPLE AND TARGET ${QT_PACKAGE}::qmake AND NOT TARGET ${QT_PACKAGE}::macdeployqt)
    get_target_property(_qt_qmake_location ${QT_PACKAGE}::qmake IMPORTED_LOCATION)

    execute_process(
        COMMAND "${_qt_qmake_location}" -query QT_INSTALL_PREFIX
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE qt_install_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(imported_location "${qt_install_prefix}/bin/macdeployqt")

    if(EXISTS ${imported_location})
        add_executable(${QT_PACKAGE}::macdeployqt IMPORTED)
        set_target_properties(${QT_PACKAGE}::macdeployqt PROPERTIES
            IMPORTED_LOCATION ${imported_location}
        )
    endif()
endif()

# Queried unconditionally (not tied to the target-creation block above, which Qt 6.11's
# own exported Qt6::macdeployqt target skips) because the offscreen platform plugin isn't
# linked by anything, so macdeployqt never discovers or copies it -- it has to be found
# and placed by hand. Homebrew's Qt keeps plugins under "share/qt/plugins" (files shared
# across formulae live under share/), while the official installer keeps them directly
# under "plugins/"; QT_INSTALL_PLUGINS is correct for either layout.
#
# Gated on TARGET ${QT_PACKAGE}::macdeployqt, matching the consumer below exactly (the only
# thing that actually uses qt_install_plugins is gated on ::macdeployqt existing, nothing
# else). Locating the plugins directory needs qmake, so ::qmake is checked too, but *inside*
# the block rather than ANDed into its guard: that keeps the outer condition identical to the
# consumer's, and turns "qmake is missing" into an immediate, clearly-worded configure error
# instead of silently leaving qt_install_plugins unset for the consumer to fail on later with
# a cryptic `cmake -E copy ".../platforms/libqoffscreen.dylib"` (a leading slash and nothing
# else -- the empty variable). This is the failure mode this file already hit once; a Qt that
# exports macdeployqt but not qmake would otherwise reproduce it in a new form.
if(${QT_PACKAGE}_FOUND AND APPLE AND TARGET ${QT_PACKAGE}::macdeployqt)
    if(NOT TARGET ${QT_PACKAGE}::qmake)
        message(FATAL_ERROR "${QT_PACKAGE}::macdeployqt is exported but ${QT_PACKAGE}::qmake is not; "
            "locating the offscreen platform plugin (QT_INSTALL_PLUGINS) requires qmake.")
    endif()
    get_target_property(_qt_qmake_location ${QT_PACKAGE}::qmake IMPORTED_LOCATION)
    execute_process(
        COMMAND "${_qt_qmake_location}" -query QT_INSTALL_PLUGINS
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE qt_install_plugins
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT return_code EQUAL 0 OR NOT qt_install_plugins)
        message(FATAL_ERROR "Could not query QT_INSTALL_PLUGINS from ${_qt_qmake_location} (return code ${return_code})")
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

        # Anchored on CMAKE_SOURCE_DIR, matching MACOSX_BUNDLE_INFO_PLIST above, rather than
        # CMAKE_CURRENT_LIST_DIR. Inside a function(), CMAKE_CURRENT_LIST_DIR resolves against
        # the call site (today, src/cross/CMakeLists.txt, i.e. src/cross for every caller of
        # qt_executable()) rather than the file that defines the function (src/cross/widgets,
        # where Qt.cmake itself lives) -- verified with a debug message() during configure.
        # CMAKE_SOURCE_DIR has no such call-site dependency, so it stays correct even if a
        # future caller invokes qt_executable() from a different directory, where
        # CMAKE_CURRENT_LIST_DIR would silently start resolving somewhere else. Two levels up
        # from CMAKE_SOURCE_DIR (src/cross) reaches the repository root.
        set(_icns "${CMAKE_SOURCE_DIR}/../../packaging/machdbg.icns")
        target_sources(${tgt} PRIVATE "${_icns}")
        set_source_files_properties("${_icns}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        set_target_properties(${tgt} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "machdbg.icns"
        )
    endif()

    # Run macdeployqt after build to copy the Qt frameworks into the bundle and rewrite
    # its load commands. Unlike windeployqt below, this needs no once-per-directory guard:
    # TARGET_BUNDLE_DIR is the target's own <tgt>.app, a directory no other target writes
    # into, so concurrent macdeployqt runs across the four bundles cannot race each other.
    #
    # macdeployqt rewrites install names and strips the copied frameworks and plugins
    # after its own ad-hoc codesign pass, which invalidates their signatures -- it warns
    # about this itself ("codesign verification error ... invalid signature") but does not
    # correct it. On this machine that is not cosmetic: the kernel's code-signing
    # enforcement SIGKILLs the process the instant it pages in the first tainted dylib
    # (confirmed via `log show` -- "CODE SIGNING: cs_invalid_page ... denying page sending
    # SIGKILL"), so every bundle died before showing a window. Re-signing ad-hoc after
    # macdeployqt fixes the signature and lets the bundle launch.
    #
    # macdeployqt also only deploys the platform plugin(s) the build actually needs to
    # display a window (libqcocoa.dylib here), not the offscreen one -- there is no
    # linked reference to it for macdeployqt to discover. The offscreen platform is what
    # CI selects via QT_QPA_PLATFORM=offscreen (no window server there), so it is copied
    # in by hand from Qt's own plugin directory.
    #
    # That raw copy is not enough on its own: unlike libqcocoa.dylib, which macdeployqt
    # rewrote to load QtGui from @executable_path/../Frameworks, the hand-copied
    # libqoffscreen.dylib still references it via @rpath, and its only LC_RPATH
    # (@loader_path/../../../../lib) resolves to a build-machine path that does not exist
    # in the bundle. It happened to load anyway in testing only because dyld had already
    # mapped the bundled QtGui by the time the plugin needed it and reused that image --
    # a coincidence of load order, not a property of the bundle -- so an explicit rpath
    # into the bundle's own Frameworks directory is added before signing, making the
    # bundle correct rather than merely lucky.
    #
    # The final codesign --verify is not decorative: it is the one line in this whole
    # chain that would have caught the original SIGKILL bug (an invalid signature that
    # codesign itself can detect) before the bundle ever shipped, instead of relying on
    # someone actually launching it to notice.
    if(APPLE AND TARGET ${QT_PACKAGE}::macdeployqt)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${QT_PACKAGE}::macdeployqt "$<TARGET_BUNDLE_DIR:${tgt}>" -always-overwrite
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${tgt}>/Contents/PlugIns/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy "${qt_install_plugins}/platforms/libqoffscreen.dylib" "$<TARGET_BUNDLE_DIR:${tgt}>/Contents/PlugIns/platforms/libqoffscreen.dylib"
            COMMAND install_name_tool -add_rpath @loader_path/../../Frameworks "$<TARGET_BUNDLE_DIR:${tgt}>/Contents/PlugIns/platforms/libqoffscreen.dylib"
            COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:${tgt}>"
            COMMAND codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${tgt}>"
            COMMENT "Running macdeployqt on ${tgt}, adding the offscreen platform plugin, re-signing, and verifying..."
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
