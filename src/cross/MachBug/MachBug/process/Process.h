#pragma once

#include <sys/types.h>
#include <mach/mach.h>
#include <string>

#include <MachBug/types/MachBug.h>
#include <MachBug/types/Global.h>

namespace MachBug
{
    // The structural divergence from ElfBug::Process: ptrace addresses a Linux process purely
    // by pid, so ElfBug::Process holds only that. On macOS the pid identifies the process to
    // the outside world (signals, waitpid-adjacent bookkeeping), but every real debugging
    // operation -- task_for_pid's own result, task_set_exception_ports, mach_vm_read/write,
    // thread_get_state -- goes through the mach_port_t task port instead. This class therefore
    // holds both. That extra handle, and the fact that it is a Mach message target rather than
    // a kernel object addressed by an integer id, is the reason ElfBug's waitpid loop and
    // MachBug's mach_msg loop cannot share a code path -- not just a naming difference.
    class Process
    {
    public:
        pid_t pid;
        mach_port_t task;

        Process(pid_t pid, mach_port_t task);
        ~Process();

        Process(const Process &) = delete;
        Process & operator=(const Process &) = delete;
        Process(Process &&) = delete;
        Process & operator=(Process &&) = delete;

        // Translates a kern_return_t into a stable, named diagnostic string. Every Mach call in
        // this engine that can fail routes its failure through this one function, so a denied
        // task_for_pid -- the single most common failure in the whole product -- never surfaces
        // as "unknown error".
        static std::string DescribeKernReturn(kern_return_t kr);

        // True if a waitpid() status represents an actual termination (WIFEXITED or
        // WIFSIGNALED), as opposed to a WIFSTOPPED job-control/ptrace stop notification. A child
        // that Debugger::Start() has ptrace(PT_ATTACHEXC)'d (see Debugger.Loop.cpp) reports
        // ordinary ptrace stops through waitpid() too -- for instance while one of its threads is
        // parked on an unanswered Mach exception reply -- in a status that is indistinguishable
        // from a real exit ((int)status alone) unless this is checked explicitly. Treating any
        // waitpid() return of a pid as proof of exit, unconditionally, silently announces a live
        // target as dead.
        static bool IsRealExit(int status);

        // The exit code a status IsRealExit() has already confirmed represents: WEXITSTATUS()
        // for a normal exit, -WTERMSIG() for one killed by a signal. Mirrors ElfBug's convention.
        static int ExitCodeFromStatus(int status);

        // Polls waitpid(pid, ..., WNOHANG) until the process actually terminates (IsRealExit()
        // true) or a generous bound elapses, discarding any WIFSTOPPED notification in between --
        // see IsRealExit()'s comment for why a single check is not enough to conclude a traced
        // child has died: there can be more than one stop notification queued ahead of the
        // eventual death, and picking up one of those instead (e.g. right after sending SIGKILL,
        // to confirm it took effect) means observing a status that looks like an exit but is not
        // one -- or, worse, simply not noticing the child is still alive at all. Writes the exit
        // code to *exitCode (mirroring ExitCodeFromStatus()) when it is not null. Returns false if
        // pid could never be waited on at all (waitpid() itself failed, e.g. ECHILD -- already
        // reaped, or never existed) or if the bound elapsed without observing a real exit.
        //
        // Polled rather than a single blocking waitpid(pid, &status, 0): milestone 2 task 6
        // measured (see its task report) that a blocking waitpid() issued on this same kind of
        // pid can simply never return, even though the identical state change is visible moments
        // later to a WNOHANG poll of the same pid -- reproduced for a call immediately following
        // a fresh ptrace(PT_ATTACHEXC), but this function has no such guarantee about every
        // caller's history with pid (Terminate(), exceptionLoop()'s own teardown), so the same
        // protection is worth having here too. This also bounds what used to be an unconditional
        // block: a caller reaching this function after DetachAndKill()'s SIGKILL has, until now,
        // had no way to fail loudly and quickly if pid somehow never dies -- an open-ended
        // waitpid() just makes that look like a slow test, not a failing one.
        static bool WaitForRealExit(pid_t pid, int* exitCode);

        // Detaches ptrace, then SIGKILLs pid, then waits (WaitForRealExit()) until it actually
        // dies or that wait's own bound elapses. The detach has to come first: a pid that is
        // still ptrace(PT_ATTACHEXC)'d (Debugger::Start()) routes SIGKILL through its Mach
        // exception port exactly like any other signal, and a caller reaching for this is, by
        // construction, one that has already stopped servicing that port -- the signal is then
        // never answered, and the pid becomes unkillable by this call or by an external `kill -9`
        // alike (measured: it does not merely take longer, it does not happen at all). PT_DETACH
        // is a harmless, ignored-on-failure no-op when pid was never ptrace-attached in the first
        // place -- e.g. a child Init() launched but whose Start() never ran -- so this is always
        // the right thing to call before killing a child this class knows about, not only after
        // a live Start().
        //
        // Returns false only if the kill() itself failed (e.g. the pid was already gone); still
        // returns true if WaitForRealExit() times out (this function does not escalate a wait
        // timeout into a call failure -- doing so here would have no way to reach a caller that
        // could safely act on it without reintroducing the unbounded block WaitForRealExit()
        // exists to avoid). That timeout is not silent, though: it is logged to stderr, naming
        // the pid, precisely because every one of this function's callers (Terminate(),
        // exceptionLoop()'s teardown, Init()'s task_for_pid-denial cleanup) discards this return
        // value, and a target that would not die is the one failure mode this codebase has
        // already produced four Criticals investigating. *exitCode mirrors WaitForRealExit()'s.
        static bool DetachAndKill(pid_t pid, int* exitCode);
    };
}
