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
    };
}
