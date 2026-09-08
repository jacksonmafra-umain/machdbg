// The MIG entry point: catch_mach_exception_raise_state_identity(), the routine
// mach_exc_server() (called from Debugger::exceptionLoop(), Debugger.Loop.cpp) invokes for every
// EXCEPTION_STATE_IDENTITY exception message. It is declared in mach_exc.h -- generated at
// configure time from mach_exc.defs by MachBugMig.cmake -- with C linkage and no extern "C"
// guard around that declaration.
//
// THE TRAP (see MachBugMigStubs.c for the long version): MachBug::Debugger is a C++ class, so
// the natural way to write this is as a member function, but mach_exc.h's prototype is plain C.
// A definition here that is not *also* declared extern "C" gets its name mangled by the C++
// compiler; the mangled symbol does not match the weak C symbol MachBugMigStubs.c provides as a
// fallback, so the linker has nothing to complain about -- it silently keeps the weak stub
// instead, and every exception comes back KERN_FAILURE forever, with no build or link error
// anywhere. `extern "C"` on the function below is what makes this definition actually override
// that weak stub. See "How this was verified" in the task report for how that override was
// confirmed rather than assumed.
//
// catch_mach_exception_raise() and catch_mach_exception_raise_state() are NOT redefined here:
// MachBugMigStubs.c already provides both, permanently and non-weak, since this debugger only
// ever requests EXCEPTION_STATE_IDENTITY (see task_set_exception_ports() in
// Debugger.Loop.cpp::Start()) and neither of those two routines is ever expected to be reached.
// Defining them again here would be a duplicate (non-weak) symbol and fail to link.

#include <MachBug/core/Debugger.h>

extern "C" {
#include "mach_exc.h"
}

extern "C" kern_return_t catch_mach_exception_raise_state_identity(
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
    (void)exception_port;
    (void)task;
    (void)flavor;
    (void)old_state;
    (void)old_stateCnt;

    // task_set_exception_ports() (Debugger.Loop.cpp::Start()) requests a real state flavor --
    // EXCEPTION_STATE_IDENTITY requires one; THREAD_STATE_NONE is only valid for
    // EXCEPTION_DEFAULT and silently prevents delivery entirely if paired with a state-carrying
    // behavior like this one (see that call site's comment). old_state/old_stateCnt above are
    // simply not read: this loop has no present need for the register state a *_STATE_IDENTITY
    // message optionally carries, so it goes unused rather than unrequested. new_stateCnt = 0
    // tells the kernel the reply carries no replacement state, i.e. leave the thread's state
    // exactly as it was reported -- this is valid regardless of what flavor old_state was
    // delivered in. Thread state changes (single-stepping) instead go through
    // thread_get_state()/thread_set_state() directly on `thread` -- see setSingleStep() in
    // Debugger.Loop.cpp.
    if(new_stateCnt)
        *new_stateCnt = 0;

    MachBug::Debugger* debugger = MachBug::Debugger::Current();
    if(!debugger)
    {
        // No Debugger's exceptionLoop() is dispatching on this thread. This should not be
        // reachable in practice -- mach_exc_server() is only ever called from inside
        // Debugger::exceptionLoop(), which sets this before calling it -- but failing loudly
        // here costs nothing and a silent KERN_FAILURE with no diagnostic would be a much
        // harder bug to find later.
        return KERN_FAILURE;
    }

    return debugger->handleException(thread, exception, code, codeCnt);
}
