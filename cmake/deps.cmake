if(CMAKE_SCRIPT_MODE_FILE)
    set(GUI_DLL "${CMAKE_ARGV3}")
    set(DEPS_DIR "${CMAKE_ARGV4}")
    set(WINDEPLOYQT "${CMAKE_ARGV5}")
    set(EXPECTED_DEPS_HASH "${CMAKE_ARGV6}")
    get_filename_component(GUI_DIR "${GUI_DLL}" DIRECTORY)
    get_filename_component(WINDEPLOYQT_DIR "${WINDEPLOYQT}" DIRECTORY)
    set(DEPS_COPIED_FILE "${GUI_DIR}/.deps_copied")
    set(DEPS_HASH_FILE "${GUI_DIR}/.deps_hash")

    # Check if we already copied the dependencies for the current Qt/deps state.
    if(EXISTS "${DEPS_COPIED_FILE}" AND EXISTS "${DEPS_HASH_FILE}")
        file(READ "${DEPS_HASH_FILE}" CURRENT_DEPS_HASH)
        string(STRIP "${CURRENT_DEPS_HASH}" CURRENT_DEPS_HASH)
        if("${CURRENT_DEPS_HASH}" STREQUAL "${EXPECTED_DEPS_HASH}")
            return()
        endif()
    endif()

    # Make windeployqt resilient against globally configured Qt environments.
    string(CONCAT SANITIZED_PATH "${WINDEPLOYQT_DIR}" ";" "$ENV{PATH}")
    set(ENV{PATH} "${SANITIZED_PATH}")
    unset(ENV{QTDIR})
    unset(ENV{QT_PLUGIN_PATH})
    unset(ENV{QT_QPA_PLATFORM_PLUGIN_PATH})
    unset(ENV{QML_IMPORT_PATH})
    unset(ENV{QML2_IMPORT_PATH})

    message(STATUS "Copying dependencies from ${DEPS_DIR} to ${GUI_DIR}")

    execute_process(
        COMMAND "${WINDEPLOYQT}" --pdb --no-compiler-runtime --no-translations --no-opengl-sw --force "${GUI_DLL}" --list relative
        WORKING_DIRECTORY "${GUI_DIR}"
        RESULT_VARIABLE WINDEPLOYQT_RESULT
        OUTPUT_VARIABLE DEPS_COPIED
        ERROR_VARIABLE WINDEPLOYQT_STDERR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )

    if(NOT "${WINDEPLOYQT_STDERR}" STREQUAL "")
        message(STATUS "${WINDEPLOYQT_STDERR}")
    endif()

    if(NOT WINDEPLOYQT_RESULT EQUAL 0)
        message(FATAL_ERROR "windeployqt failed with exit code ${WINDEPLOYQT_RESULT}")
    endif()

    # Split the output into lines
    string(REGEX REPLACE "\n" ";" DEPS_COPIED "${DEPS_COPIED}")
    list(FILTER DEPS_COPIED EXCLUDE REGEX "^$")
    foreach(line ${DEPS_COPIED})
        message(STATUS "Copying ${line}")
    endforeach()

    function(copy_dep relfile)
        set(DEPS_COPIED ${DEPS_COPIED} ${relfile} PARENT_SCOPE)
        set(target_path "${GUI_DIR}/${relfile}")
        if(IS_SYMLINK "${target_path}")
            message(STATUS "Skipping symlink ${relfile}")
            return()
        endif()
        message(STATUS "Copying ${relfile}")
        get_filename_component(reldir "${relfile}" DIRECTORY)
        get_filename_component(filename "${relfile}" NAME)
        if(reldir)
            file(COPY "${DEPS_DIR}/${reldir}/${filename}" DESTINATION "${GUI_DIR}/${reldir}")
        else()
            file(COPY "${DEPS_DIR}/${filename}" DESTINATION "${GUI_DIR}")
        endif()
    endfunction()

    file(GLOB_RECURSE DEPS RELATIVE "${DEPS_DIR}" "${DEPS_DIR}/*.dll")
    list(SORT DEPS)
    foreach(DEP ${DEPS})
        copy_dep("${DEP}")
    endforeach()

    list(JOIN DEPS_COPIED "\n" DEPS_COPIED)
    file(WRITE "${DEPS_COPIED_FILE}" "${DEPS_COPIED}")
    file(WRITE "${DEPS_HASH_FILE}" "${EXPECTED_DEPS_HASH}\n")

    return()
endif()

if(NOT WIN32)
    message(STATUS "copy_dependencies is only supported on Windows")
    return()
endif()

if(NOT TARGET Qt5::windeployqt)
    if(Qt5_FOUND AND TARGET Qt5::qmake)
        get_target_property(_qt5_qmake_location Qt5::qmake IMPORTED_LOCATION)

        execute_process(
            COMMAND "${_qt5_qmake_location}" -query QT_INSTALL_PREFIX
            RESULT_VARIABLE return_code
            OUTPUT_VARIABLE qt5_install_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(imported_location "${qt5_install_prefix}/bin/windeployqt.exe")
        if(EXISTS ${imported_location})
            add_executable(Qt5::windeployqt IMPORTED)

            set_target_properties(Qt5::windeployqt PROPERTIES
                IMPORTED_LOCATION ${imported_location}
            )
        endif()
    endif()

    if(NOT TARGET Qt5::windeployqt)
        # Fallback: search for windeployqt on PATH
        find_program(_qt5_windeployqt_from_path NAMES windeployqt.exe windeployqt)
        if(_qt5_windeployqt_from_path)
            add_executable(Qt5::windeployqt IMPORTED)
            set_target_properties(Qt5::windeployqt PROPERTIES
                IMPORTED_LOCATION ${_qt5_windeployqt_from_path}
            )
        endif()
    endif()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DEPS_DIR ${CMAKE_SOURCE_DIR}/deps/x64)
else()
    set(DEPS_DIR ${CMAKE_SOURCE_DIR}/deps/x32)
endif()

get_target_property(_qt5_windeployqt_location Qt5::windeployqt IMPORTED_LOCATION)
if(NOT _qt5_windeployqt_location)
    message(FATAL_ERROR "Could not locate Qt5::windeployqt. Install Qt with windeployqt and ensure it is discoverable (in PATH or via Qt5::qmake).")
endif()

file(GLOB_RECURSE DEPS_INPUT_DLLS "${DEPS_DIR}/*.dll")
list(SORT DEPS_INPUT_DLLS)

file(SHA256 "${_qt5_windeployqt_location}" WINDEPLOYQT_HASH)
set(DEPS_HASH_INPUT "WINDEPLOYQT=${WINDEPLOYQT_HASH}\n")
foreach(DEP_FILE ${DEPS_INPUT_DLLS})
    file(RELATIVE_PATH DEP_RELATIVE "${DEPS_DIR}" "${DEP_FILE}")
    file(SHA256 "${DEP_FILE}" DEP_HASH)
    string(APPEND DEPS_HASH_INPUT "${DEP_RELATIVE}=${DEP_HASH}\n")
endforeach()
string(SHA256 EXPECTED_DEPS_HASH "${DEPS_HASH_INPUT}")

add_custom_target(deps
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/deps.cmake $<TARGET_FILE:gui> ${DEPS_DIR} $<TARGET_FILE:Qt5::windeployqt> ${EXPECTED_DEPS_HASH}
)

# Make a rebuild copy the dependencies again
set_target_properties(deps PROPERTIES
    ADDITIONAL_CLEAN_FILES "$<TARGET_FILE_DIR:gui>/.deps_copied;$<TARGET_FILE_DIR:gui>/.deps_hash"
)
