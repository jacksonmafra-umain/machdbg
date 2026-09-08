// The MIG entry point: catch_mach_exception_raise_state_identity(), the routine
// mach_exc_server() (called from Debugger::exceptionLoop(), Debugger.Loop.cpp) invokes for every
// EXCEPTION_STATE_IDENTITY exception message. It is declared in mach_exc.h -- generated at
// configure time from mach_exc.defs by MachBugMig.cmake -- and that declaration is itself
// unguarded: mach_exc.h has no extern "C" block around the catch_* prototypes (its own extern "C"
// blocks cover only voucher_mach_msg_set and mig_strncpy_zerofill, unrelated symbols). What gives
// the prototype -- and therefore this definition -- C linkage is the `extern "C" { ... }` wrapped
// around the #include below, in this file. That is a correction to an earlier, wrong claim (in
// the original version of this comment, and in one commit's reasoning) that mach_exc.h supplied
// its own guard here; it does not, this file's own wrapping does.
//
// THE TRAP (see MachBugMigStubs.c for the long version, including why it once had a weak
// fallback and no longer does): MachBug::Debugger is a C++ class, so the natural way to write
// this is as a member function, but the prototype above needs C linkage. A definition here that
// is not *also* declared extern "C" gets its name mangled by the C++ compiler -- and, since
// MachBugMigStubs.c no longer provides a weak fallback for this symbol, a mangled definition
// simply never resolves the reference mach_excServer.c.o makes to it: an undefined-symbol error
// at link time, not a silent runtime KERN_FAILURE the way it used to be. `extern "C"` on the
// function below (by way of the `extern "C" { #include "mach_exc.h" }` block, which is what
// actually establishes the linkage this definition then matches) is what keeps this correct.
//
// catch_mach_exception_raise() and catch_mach_exception_raise_state() are NOT redefined here:
// MachBugMigStubs.c already provides both, permanently, since this debugger only ever requests
// EXCEPTION_STATE_IDENTITY (see task_set_exception_ports() in Debugger.Loop.cpp::Start()) and
// neither of those two routines is ever expected to be reached. Defining them again here would
// be a duplicate symbol and fail to link.

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

    kern_return_t result;
    MachBug::Debugger* debugger = MachBug::Debugger::Current();
    if(!debugger)
    {
        // No Debugger's exceptionLoop() is dispatching on this thread. This should not be
        // reachable in practice -- mach_exc_server() is only ever called from inside
        // Debugger::exceptionLoop(), which sets this before calling it -- but failing loudly
        // here costs nothing and a silent KERN_FAILURE with no diagnostic would be a much
        // harder bug to find later.
        result = KERN_FAILURE;
    }
    else
    {
        result = debugger->handleException(thread, exception, code, codeCnt);
    }

    // `thread` and `task` arrive as send rights this call now owns -- mach_exc.defs' generated
    // server disposes of them as moveSend, and neither mach_exc_server() nor this function
    // deallocated them before. `task` is never used at all (mProcess already holds this
    // Debugger's own right to the same task); `thread` is used, when it is at all, only
    // synchronously within handleException() (thread_get_state()/thread_set_state() in
    // setSingleStep(), Debugger.Loop.cpp) and not retained past it. Leaking two port names per
    // exception is invisible at a single stop and fatal to a stepping loop that raises many --
    // Milestone 4's problem to have hit if this weren't fixed here.
    mach_port_deallocate(mach_task_self(), thread);
    mach_port_deallocate(mach_task_self(), task);

    return result;
}
