#include <MachBug/core/Debugger.h>

#include <spawn.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <vector>

// Not declared in <unistd.h> on macOS; the child's environment is inherited via posix_spawn's
// explicit envp argument, which needs this to pass the parent's own environment through.
extern char** environ;

// Init()/Terminate()/spawnSuspended() live in this file, unchanged since Task 3 -- they only
// launch the child and hold its pid/task port. Start(), Continue(), StepInto(), Pause(), Stop(),
// handleException() and the receive loop they share are in Debugger.Loop.cpp; the MIG entry
// point that calls into handleException() is in ExceptionServer.cpp. See the class comment in
// Debugger.h for why the loop cannot be shaped like ElfBug's debugLoop()/waitpid().
namespace MachBug
{
    Debugger::Debugger() = default;

    Debugger::~Debugger()
    {
        Terminate();
    }

    bool Debugger::Init(const char* szFilePath, const char* const* argv)
    {
        // A previous launch, if any, is torn down before starting a new one -- Init() is not
        // additive.
        Terminate();

        if(!szFilePath)
            return false;

        if(access(szFilePath, X_OK) != 0)
        {
            cbInternalError("cannot execute '" + std::string(szFilePath) + "': " +
                             std::string(strerror(errno)));
            return false;
        }

        pid_t pid = 0;
        if(!spawnSuspended(szFilePath, argv, &pid))
            return false;

        // The child exists, suspended, having executed nothing yet. task_for_pid is the one
        // step in this whole sequence that can fail for a reason outside our control (the
        // target not carrying get-task-allow), so its kern_return_t is always translated
        // through Process::DescribeKernReturn rather than reported as a bare number.
        mach_port_t task = MACH_PORT_NULL;
        const kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
        if(kr != KERN_SUCCESS)
        {
            cbInternalError("task_for_pid failed for pid " + std::to_string(pid) + ": " +
                             Process::DescribeKernReturn(kr));

            // Nothing will ever resume this child -- there is no task port to install
            // exception ports through, and no other path back to it -- so kill it now rather
            // than leaking a permanently suspended process. SIGKILL terminates a stopped
            // process directly; it does not need to be resumed first.
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
            return false;
        }

        mProcess = std::make_unique<Process>(pid, task);
        return true;
    }

    bool Debugger::spawnSuspended(const char* szFilePath, const char* const* argv, pid_t* outPid)
    {
        posix_spawnattr_t attr;
        if(posix_spawnattr_init(&attr) != 0)
        {
            cbInternalError("posix_spawnattr_init failed: " + std::string(strerror(errno)));
            return false;
        }

        // The child exists after posix_spawn returns but has executed no instruction while this
        // flag is set -- it is created stopped, the same way a SIGSTOP'd process is stopped.
        // That window is the deliverable: Task 4 installs exception ports in it, before the
        // target can run and race the debugger.
        if(posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED) != 0)
        {
            const std::string err = strerror(errno);
            posix_spawnattr_destroy(&attr);
            cbInternalError("posix_spawnattr_setflags(POSIX_SPAWN_START_SUSPENDED) failed: " + err);
            return false;
        }

        std::vector<char*> argvPtrs;
        if(argv)
        {
            for(const char* const* p = argv; *p != nullptr; ++p)
                argvPtrs.push_back(const_cast<char*>(*p));
        }
        else
        {
            argvPtrs.push_back(const_cast<char*>(szFilePath));
        }
        argvPtrs.push_back(nullptr);

        pid_t pid = 0;
        const int rc = posix_spawn(&pid, szFilePath, nullptr, &attr, argvPtrs.data(), environ);
        posix_spawnattr_destroy(&attr);

        if(rc != 0)
        {
            cbInternalError("posix_spawn failed for '" + std::string(szFilePath) + "': " +
                             std::string(strerror(rc)));
            return false;
        }

        *outPid = pid;
        return true;
    }

    pid_t Debugger::GetPid() const
    {
        return mProcess ? mProcess->pid : 0;
    }

    mach_port_t Debugger::GetTaskPort() const
    {
        return mProcess ? mProcess->task : MACH_PORT_NULL;
    }

    bool Debugger::Terminate()
    {
        if(!mProcess)
            return false;

        const pid_t pid = mProcess->pid;
        mProcess.reset();

        // Process::DetachAndKill(), not a bare kill()+waitpid(): if Start() ran, this pid is
        // ptrace(PT_ATTACHEXC)'d (Debugger.Loop.cpp::Start()) -- possibly still, if Start()'s own
        // loop ended some other way than Stop() (an unexpected mach_msg failure, say) without
        // ever tearing that down. A plain SIGKILL sent to a still-attached pid is routed through
        // its Mach exception port exactly like any other signal, and with nothing left to service
        // that port, is never answered -- the pid does not die, at all, not even slower. See that
        // helper's comment in Process.h for the measured failure mode this once was.
        return Process::DetachAndKill(pid, nullptr);
    }

    void Debugger::cbInternalError(const std::string & error) { (void)error; }
}
