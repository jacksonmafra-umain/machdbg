# Generates the Mach exception server, at BUILD time, from whichever SDK xcrun resolves
# right now. Run via `cmake -P` by the machbug_mig_generate target in MachBugMig.cmake --
# read that file first for why the SDK cannot be resolved once at configure time and baked
# into a generation rule.
#
# Requires MACHBUG_MIG (the mig binary) and MACHBUG_MIG_DIR (where the generated files go).
#
# Generates into a scratch directory and copies each file over its committed output only if
# the bytes differ. That is what makes running on every build cheap: an unchanged SDK leaves
# mach_excServer.c and mach_exc.h with their existing mtimes, so nothing that compiles or
# includes them recompiles, and the whole cost is one mig invocation.

if(NOT MACHBUG_MIG OR NOT MACHBUG_MIG_DIR)
    message(FATAL_ERROR "MachBugMigGenerate.cmake needs -DMACHBUG_MIG=<mig> -DMACHBUG_MIG_DIR=<dir>")
endif()

execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE sdk_path
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE xcrun_error
    RESULT_VARIABLE xcrun_result
)
if(NOT xcrun_result EQUAL 0 OR sdk_path STREQUAL "")
    message(FATAL_ERROR
        "xcrun --show-sdk-path failed, so the Mach exception server cannot be generated: "
        "${xcrun_error}")
endif()

execute_process(
    COMMAND xcrun --show-sdk-version
    OUTPUT_VARIABLE sdk_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
# An SDK can answer --show-sdk-path and still have no version to report (a hand-assembled or
# relocated one, for instance). The stamp records what it said rather than pretending.
if(sdk_version STREQUAL "")
    set(sdk_version "unreported")
endif()

set(defs "${sdk_path}/usr/include/mach/mach_exc.defs")
if(NOT EXISTS "${defs}")
    message(FATAL_ERROR
        "mach_exc.defs not found at ${defs}. The SDK xcrun resolves (${sdk_path}) either is "
        "not a macOS SDK or does not ship the Mach interface definitions this engine's "
        "exception server is generated from.")
endif()

set(scratch "${MACHBUG_MIG_DIR}/scratch")
file(REMOVE_RECURSE "${scratch}")
file(MAKE_DIRECTORY "${scratch}")

execute_process(
    COMMAND "${MACHBUG_MIG}"
            # mach_exc.defs pulls in mach/std_types.defs and friends by angle-bracket
            # #include; without -isysroot mig's preprocessor cannot find them and fails
            # before reaching mach_exc.defs itself.
            -isysroot "${sdk_path}"
            -server  "${scratch}/mach_excServer.c"
            -sheader "${scratch}/mach_exc.h"
            # Only -server and -sheader are wanted. mach_exc.defs declares three routines, so
            # mig always co-generates the *user* (client) stubs -- what a caller sending an
            # exception message would link. A debugger only ever receives those messages, so
            # those two outputs are dead weight; mig has no flag to skip writing them, but
            # /dev/null gets the same effect without it complaining about a missing output.
            -header  /dev/null
            -user    /dev/null
            "${defs}"
    WORKING_DIRECTORY "${scratch}"
    ERROR_VARIABLE mig_error
    RESULT_VARIABLE mig_result
)
if(NOT mig_result EQUAL 0)
    message(FATAL_ERROR "mig failed against ${defs}: ${mig_error}")
endif()

# The stamp is what turns "regenerated" from an inference into a recorded fact: it names the
# SDK the files next to it were generated from, so a build log line below (and anyone
# inspecting the build tree later) can say which SDK produced the wire format currently
# compiled in, rather than assuming it was this one.
set(stamp "${MACHBUG_MIG_DIR}/mach_exc.sdk-stamp")
set(stamp_now "${sdk_path}\nSDK version ${sdk_version}\n")
set(stamp_before "")
if(EXISTS "${stamp}")
    file(READ "${stamp}" stamp_before)
endif()

set(changed FALSE)
foreach(generated mach_excServer.c mach_exc.h)
    file(SHA256 "${scratch}/${generated}" fresh_hash)
    set(existing_hash "")
    if(EXISTS "${MACHBUG_MIG_DIR}/${generated}")
        file(SHA256 "${MACHBUG_MIG_DIR}/${generated}" existing_hash)
    endif()
    if(NOT fresh_hash STREQUAL existing_hash)
        set(changed TRUE)
    endif()
    file(COPY_FILE "${scratch}/${generated}" "${MACHBUG_MIG_DIR}/${generated}" ONLY_IF_DIFFERENT)
endforeach()

file(WRITE "${stamp}" "${stamp_now}")
file(REMOVE_RECURSE "${scratch}")

# Loud only when something actually moved. A silent no-op is the normal case and does not
# deserve a line in every build log; a changed exception-message wire format very much does,
# and so does the SDK swap that caused it.
if(changed)
    message(STATUS "Mach exception server regenerated from ${defs} (SDK ${sdk_version})")
elseif(NOT stamp_before STREQUAL stamp_now)
    message(STATUS "Mach exception server now generated from ${defs} (SDK ${sdk_version}); "
                   "the generated code is byte-identical to the previous SDK's")
endif()
