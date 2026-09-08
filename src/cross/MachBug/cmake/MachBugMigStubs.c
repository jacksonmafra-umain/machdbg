/*
 * Default handlers for the two mach_exc routines the debugger never uses.
 *
 * mach_exc.defs declares three routines, so MIG's generated dispatch table
 * (mach_exc_server, in mach_excServer.c) references three catch_* symbols
 * unconditionally, whether or not the caller cares about all of them. This
 * debugger only asks for exceptions via the state-identity variant (see
 * task_set_exception_ports / thread_set_exception_ports call sites), so
 * catch_mach_exception_raise and catch_mach_exception_raise_state are dead
 * code paths that must still exist for the generated server to link.
 *
 * They are defined here, permanently, returning KERN_FAILURE: nothing is
 * ever expected to call them, and if the kernel ever did route a plain
 * EXCEPTION_DEFAULT or EXCEPTION_STATE message to this port, failing loudly
 * beats silently discarding it.
 *
 * catch_mach_exception_raise_state_identity is declared weak here for a
 * different reason: it is the routine the real exception loop cares about,
 * and its real implementation belongs with the debugger core, not with this
 * MIG plumbing target. Defining it weakly lets machbug_mig link and be
 * exercised on its own (see MachBug/tests) before that implementation
 * exists; once it does, the strong (non-weak) definition there is what the
 * linker picks, and this fallback is never reached. Do not remove the weak
 * one here without confirming a strong definition is linked everywhere
 * machbug_mig is used.
 *
 * THE TRAP: a C++ override of this symbol MUST be declared `extern "C"`.
 * mach_exc.h has no extern "C" guard around the catch_* prototypes, so a
 * plain C++ definition (the natural thing to write inside MachBug::Debugger,
 * a C++ class) gets its name mangled. A mangled strong symbol does not match
 * this weak C symbol, so the linker has nothing to complain about -- it
 * silently keeps this weak stub instead. The result is a debugger that
 * builds, links and runs, but returns KERN_FAILURE for every exception
 * forever. The fallback below writes to stderr the first time it is
 * reached specifically so that failure is loud instead of silent.
 */

#include <mach/exception_types.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>

kern_return_t catch_mach_exception_raise(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt)
{
    (void)exception_port;
    (void)thread;
    (void)task;
    (void)exception;
    (void)code;
    (void)codeCnt;
    return KERN_FAILURE;
}

kern_return_t catch_mach_exception_raise_state(
    mach_port_t exception_port,
    exception_type_t exception,
    const mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int* flavor,
    const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t* new_stateCnt)
{
    (void)exception_port;
    (void)exception;
    (void)code;
    (void)codeCnt;
    (void)flavor;
    (void)old_state;
    (void)old_stateCnt;
    (void)new_state;
    (void)new_stateCnt;
    return KERN_FAILURE;
}

static void mach_bug_mig_warn_state_identity_stub_reached(void)
{
    fprintf(stderr,
        "MachBug: the weak MIG stub for catch_mach_exception_raise_state_identity was "
        "reached -- no strong definition was linked, so every exception will be reported "
        "as KERN_FAILURE. If the real handler is C++, it must be declared extern \"C\": "
        "mach_exc.h has no extern \"C\" guard, so a plain C++ definition gets its name "
        "mangled and silently fails to override this weak C symbol.\n");
}

__attribute__((weak))
kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int* flavor,
    thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t* new_stateCnt)
{
    // Printed once, not on every message: an exception loop can be hot, and
    // once is already enough for a person reading a test failure to notice.
    static pthread_once_t warned_once = PTHREAD_ONCE_INIT;
    pthread_once(&warned_once, mach_bug_mig_warn_state_identity_stub_reached);

    (void)exception_port;
    (void)thread;
    (void)task;
    (void)exception;
    (void)code;
    (void)codeCnt;
    (void)flavor;
    (void)old_state;
    (void)old_stateCnt;
    (void)new_state;
    (void)new_stateCnt;
    return KERN_FAILURE;
}
