#include <MachBug/process/Process.h>

#include <mach/mach_error.h>
#include <sys/wait.h>

namespace MachBug
{
    Process::Process(const pid_t pid, const mach_port_t task)
        : pid(pid)
        , task(task)
    {
    }

    Process::~Process()
    {
        // task_for_pid handed back a send right to the task port that this Process now owns;
        // release it so the port does not leak once we are done with the process.
        if(task != MACH_PORT_NULL)
            mach_port_deallocate(mach_task_self(), task);
    }

    std::string Process::DescribeKernReturn(const kern_return_t kr)
    {
        switch(kr)
        {
        case KERN_SUCCESS:
            return "KERN_SUCCESS";

        case KERN_FAILURE:
            // Measured (docs/specs/2026-09-07-macos-port-design.md, section 9): from a same-user
            // caller with no debugger entitlement, task_for_pid returns exactly this code when
            // the target is not signed with com.apple.security.get-task-allow, and succeeds
            // when it is. This is the single most common failure in the whole product, so it
            // gets a named diagnostic rather than falling through to mach_error_string's generic
            // "(os/kern) failure".
            return "KERN_FAILURE (5): task_for_pid denied -- the target most likely lacks "
                   "com.apple.security.get-task-allow, or this caller lacks the debugger "
                   "entitlement/privilege needed to reach a target that did not opt in";

        case KERN_INVALID_ARGUMENT:
            return "KERN_INVALID_ARGUMENT: invalid pid or task argument (the process may "
                   "already have exited)";

        case KERN_NO_ACCESS:
            return "KERN_NO_ACCESS: access to the task was denied";

        case KERN_INVALID_TASK:
            return "KERN_INVALID_TASK: the task port is no longer valid";

        default:
        {
            std::string result = "kern_return_t " + std::to_string(kr);
            if(const char* text = mach_error_string(kr))
            {
                result += " (";
                result += text;
                result += ")";
            }
            return result;
        }
        }
    }

    bool Process::IsRealExit(const int status)
    {
        return WIFEXITED(status) || WIFSIGNALED(status);
    }

    int Process::ExitCodeFromStatus(const int status)
    {
        return WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    }

    bool Process::WaitForRealExit(const pid_t pid, int* const exitCode)
    {
        for(;;)
        {
            int status = 0;
            const pid_t result = waitpid(pid, &status, 0);
            if(result == -1)
                return false; // e.g. ECHILD: already reaped, or never existed

            if(IsRealExit(status))
            {
                if(exitCode)
                    *exitCode = ExitCodeFromStatus(status);
                return true;
            }

            // WIFSTOPPED: not a termination -- a ptrace-visible stop (PT_ATTACHEXC, see
            // Debugger.Loop.cpp::Start()) reported through waitpid(), same as IsRealExit()'s
            // comment describes. Keep waiting for the state change that actually is one.
        }
    }
}
