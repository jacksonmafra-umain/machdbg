# Generates the Mach exception server from the SDK's mach_exc.defs and wraps
# it in a target other code can link: machbug_mig.
#
# mach_exc.defs rather than exc.defs: the mach_ variant carries 64-bit
# exception codes, which MACH_EXCEPTION_CODES requires and arm64 needs. See
# Step 5 of the task brief for how that is verified after generation.
#
# Only -server and -sheader are requested from mig. mach_exc.defs declares
# three routines, so mig always wants to co-generate a *user* (client) stub
# too -- the code a caller of mach_exception_raise/_state/_state_identity
# would link against to send an exception message. A debugger is always the
# receiver of those messages, never the sender, so the user stub and its
# companion user-side header are dead weight: generating them into the tree
# and never linking them is clutter, not safety margin. mig has no flag that
# skips writing them outright, but redirecting -user and -header to /dev/null
# gets the same effect without mig complaining about a missing output.
# -sheader (the server-side header) is what actually declares mach_exc_server
# and the catch_* callback prototypes; it is generated as mach_exc.h since
# that is the only header anything in this tree includes.
find_program(MACHBUG_MIG mig REQUIRED)

execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE MACHBUG_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(_defs "${MACHBUG_SDK_PATH}/usr/include/mach/mach_exc.defs")
if(NOT EXISTS "${_defs}")
    message(FATAL_ERROR "mach_exc.defs not found at ${_defs}")
endif()

set(MACHBUG_MIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/mig")
file(MAKE_DIRECTORY "${MACHBUG_MIG_DIR}")

add_custom_command(
    OUTPUT "${MACHBUG_MIG_DIR}/mach_excServer.c"
           "${MACHBUG_MIG_DIR}/mach_exc.h"
    COMMAND "${MACHBUG_MIG}"
            # mach_exc.defs pulls in mach/std_types.defs and friends by
            # angle-bracket #include; without -isysroot mig's preprocessor
            # cannot find them and fails before reaching mach_exc.defs itself.
            -isysroot "${MACHBUG_SDK_PATH}"
            -server  "${MACHBUG_MIG_DIR}/mach_excServer.c"
            -sheader "${MACHBUG_MIG_DIR}/mach_exc.h"
            -header  /dev/null
            -user    /dev/null
            "${_defs}"
    # DEPENDS on both the .defs file and the mig binary. MACHBUG_SDK_PATH is
    # resolved once, at configure time (see the caveat below); listing mig
    # itself here means that if a build tree survives an Xcode update without
    # being reconfigured, a *newer mig* against the *same* .defs file still
    # gets noticed (mig's mtime changed) and reruns, rather than silently
    # leaving last month's generated server in place.
    DEPENDS "${_defs}" "${MACHBUG_MIG}"
    COMMENT "Generating the Mach exception server from mach_exc.defs"
    VERBATIM
)

# The generated server declares three catch_mach_exception_raise* symbols but
# does not implement any of them -- that is what makes it a *server* someone
# else fills in, not a working exception loop. Without a definition for at
# least all three, nothing that references mach_exc_server can link. See
# MachBugMigStubs.c for what is provided permanently versus provided only
# until the real debugger loop exists.
add_library(machbug_mig STATIC
    "${MACHBUG_MIG_DIR}/mach_excServer.c"
    "${CMAKE_CURRENT_LIST_DIR}/MachBugMigStubs.c"
)
target_include_directories(machbug_mig PUBLIC "${MACHBUG_MIG_DIR}")

# Caveat on MACHBUG_SDK_PATH: xcrun --show-sdk-path runs once, at configure
# time, and its result is baked into the add_custom_command above as a plain
# (uncached) CMake variable -- it is not re-resolved on every build. A build
# tree that is reconfigured (cmake.toml's CMAKE_CONFIGURE_DEPENDS makes that
# happen whenever cmake.toml itself changes, but nothing ties it to Xcode)
# will pick up a new Xcode's SDK path and .defs file. One that is not
# reconfigured after an in-place Xcode update keeps using the previously
# resolved path: if that path still exists (typical for an Xcode point
# update, since /Applications/Xcode.app/.../MacOSX.sdk is stable across
# those), the DEPENDS on the .defs file's mtime still catches a changed
# mach_exc.defs and regenerates correctly. It is only a *removed or renamed*
# SDK path (a major Xcode version bump can do this) that this setup handles
# by failing loudly at build time ("no rule to make target", or mig itself
# erroring on a stale absolute path) rather than regenerating -- there is no
# silent-stale-and-successful case here, but there is a loud-failure case
# that requires an explicit `cmake --fresh` (or deleting the build tree) to
# recover from. Documented rather than solved: see the task report.
