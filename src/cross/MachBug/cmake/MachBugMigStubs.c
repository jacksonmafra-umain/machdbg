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
 * catch_mach_exception_raise_state_identity -- the routine the real exception
 * loop cares about -- used to be defined here too, weakly, so machbug_mig
 * could link and be exercised on its own (see MachBug/tests) before
 * MachBug::Debugger's real implementation (ExceptionServer.cpp) existed. It
 * no longer is: that need was Task 2's, and MachBug has provided a strong
 * definition since Task 4. Keeping a weak fallback around past the point
 * where something is always supposed to satisfy it strongly turned a real
 * linking mistake into a silent one -- a missing or mismatched strong
 * handler still built, linked and ran, just returning KERN_FAILURE for
 * every exception forever, which is exactly what happened here once
 * (see the task report). With the weak definition gone, that same mistake
 * is now a hard undefined-symbol error at build time instead.
 *
 * THE TRAP REMAINS REAL, for whoever next touches the real handler: a C++
 * definition of catch_mach_exception_raise_state_identity MUST be declared
 * `extern "C"`, or the linker will simply fail to find a definition for it
 * at all (now that there is no weak one to silently fall back to). The
 * catch_* prototypes in mach_exc.h are themselves unguarded -- that header
 * has no extern "C" block of its own around them (its own extern "C" blocks
 * cover only voucher_mach_msg_set and mig_strncpy_zerofill). What actually
 * gives a correct C++ definition C linkage is the includER wrapping its own
 * `#include "mach_exc.h"` in `extern "C" { ... }`, the way ExceptionServer.cpp
 * does -- not anything inside the generated header itself. Get that wrapping
 * wrong (or write the definition somewhere that never includes mach_exc.h
 * inside an extern "C" block at all) and the definition's name gets mangled,
 * unrelated to any weak symbol here, and simply never resolves.
 */

#include <mach/exception_types.h>
#include <mach/mach.h>

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
