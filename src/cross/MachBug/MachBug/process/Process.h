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

        // Blocks in waitpid(pid, ..., 0) until the process actually terminates (IsRealExit()
        // true), discarding any WIFSTOPPED notification in between -- see IsRealExit()'s comment
        // for why a single, un-looped waitpid() call is not enough to conclude a traced child has
        // died: there can be more than one stop notification queued ahead of the eventual death,
        // and picking up one of those instead (e.g. right after sending SIGKILL, to confirm it
        // took effect) means observing a status that looks like an exit but is not one -- or,
        // worse, simply not noticing the child is still alive at all. Writes the exit code to
        // *exitCode (mirroring ExitCodeFromStatus()) when it is not null. Returns false only if
        // pid could never be waited on at all (waitpid() itself failed, e.g. ECHILD -- already
        // reaped, or never existed).
        static bool WaitForRealExit(pid_t pid, int* exitCode);
    };
}
