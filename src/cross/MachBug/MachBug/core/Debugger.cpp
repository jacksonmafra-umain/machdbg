#include <MachBug/core/Debugger.h>

#include <spawn.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <vector>

// Not declared in <unistd.h> on macOS; the child's environment is inherited via posix_spawn's
// explicit envp argument, which needs this to pass the parent's own environment through.
extern char** environ;

namespace
{
    // Attach()'s WNOHANG poll for pid's own attach-SIGSTOP transition (see that method's
    // comment): interval and bound. 20ms mirrors exceptionLoop()'s kExitPollIntervalMs
    // (Debugger.Loop.cpp) -- the same validated poll shape, reused here for a different race.
    // ~5s total is generous for a transition that, when it is not instant, has been observed
    // taking up to roughly a second.
    constexpr useconds_t kAttachWaitPollIntervalUs = 20000;
    constexpr int kAttachWaitMaxAttempts = 250;
}

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

        // Init()'s own launch path establishes no ptrace relationship at all (Start() does that
        // -- see mAttachedViaPtrace's comment in Debugger.h); a prior Attach() on this same
        // object, or a prior failed task_for_pid, must not leave either flag describing this new
        // launch.
        mAttachedViaPtrace = false;
        mLastTaskForPidResult = KERN_SUCCESS;

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
        mLastTaskForPidResult = kr;
        if(kr != KERN_SUCCESS)
        {
            cbInternalError("task_for_pid failed for pid " + std::to_string(pid) + ": " +
                             Process::DescribeKernReturn(kr));

            // Nothing will ever resume this child -- there is no task port to install
            // exception ports through, and no other path back to it -- so kill it now rather
            // than leaking a permanently suspended process. SIGKILL terminates a stopped
            // process directly; it does not need to be resumed first.
            //
            // Process::WaitForRealExit(), not a bare waitpid(pid, &status, 0): the latter was a
            // single blocking call with the exact shape Task 6 measured can simply never return
            // (see that function's own comment in Process.h/.cpp) -- reusing it here, rather than
            // repeating the pattern, gets this call site the same bounded, WNOHANG-polled fix for
            // free instead of leaving a second copy of the bug it was fixed for.
            kill(pid, SIGKILL);
            Process::WaitForRealExit(pid, nullptr);
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

    bool Debugger::Attach(const pid_t pid)
    {
        // A previous launch/attach, if any, is torn down first -- same contract as Init(): this
        // call is not additive, and Start() below assumes mProcess describes exactly the process
        // this call attaches to.
        Terminate();

        mAttachedViaPtrace = false;
        mLastTaskForPidResult = KERN_SUCCESS;

        if(pid <= 0)
        {
            cbInternalError("Attach() requires a positive pid, got " + std::to_string(pid));
            return false;
        }

        // PT_ATTACHEXC, not PT_ATTACH -- see this method's comment in Debugger.h for why.
        if(ptrace(PT_ATTACHEXC, pid, nullptr, 0) != 0)
        {
            cbInternalError("ptrace(PT_ATTACHEXC) failed to attach to pid " +
                             std::to_string(pid) + ": " + std::string(strerror(errno)));
            return false;
        }

        // PT_ATTACHEXC's attach sends pid a SIGSTOP but, unlike Init()'s
        // POSIX_SPAWN_START_SUSPENDED, does not guarantee it has taken effect by the time the
        // call above returns -- ptrace(2) documents that the caller must waitpid() for it. This
        // is also what completes PT_ATTACHEXC's reparent of pid to this process for tracing
        // purposes (xnu's mach_process.c), the reason a waitpid() on pid is valid at all from
        // here on, including inside Start()'s exceptionLoop(), even when this process did not
        // itself spawn pid.
        //
        // Polled with WNOHANG rather than a single blocking waitpid(pid, &status, 0): measured
        // (see the task report) that a blocking call issued immediately after the ptrace() above
        // can simply never return, even though the identical state change is visible moments
        // later to a WNOHANG poll of the same pid -- most reproducible when pid is attached to
        // this soon after it was itself spawned, before its own exec has settled. Rather than
        // chase that race's exact mechanism, this reuses exceptionLoop()'s own validated pattern
        // (Debugger.Loop.cpp: a WNOHANG poll on a timer, there for an unrelated reason -- a clean
        // exit raises no Mach exception at all) instead of a blocking call this kernel does not
        // reliably wake.
        int status = 0;
        pid_t waited = -1;
        for(int attempt = 0; attempt < kAttachWaitMaxAttempts; ++attempt)
        {
            waited = waitpid(pid, &status, WNOHANG);
            if(waited == pid || waited < 0)
                break;
            usleep(kAttachWaitPollIntervalUs);
        }
        if(waited != pid)
        {
            cbInternalError("waitpid() after ptrace(PT_ATTACHEXC) failed for pid " +
                             std::to_string(pid) + ": " + std::string(strerror(errno)));
            ptrace(PT_DETACH, pid, reinterpret_cast<caddr_t>(1), 0);
            return false;
        }

        if(Process::IsRealExit(status))
        {
            // pid exited (or was killed) before its own attach-SIGSTOP could take effect --
            // nothing is left to debug. Not this call's place to raise cbExitProcessEvent(): no
            // Start() loop is running yet to have reported this pid as a live target in the
            // first place.
            cbInternalError("pid " + std::to_string(pid) + " exited before attach completed");
            return false;
        }

        // pid is now confirmed stopped (WIFSTOPPED, via !IsRealExit() above) -- the same window
        // POSIX_SPAWN_START_SUSPENDED gives Init(), just reached by a SIGSTOP instead of a
        // suspended exec. Safe to take the task port and let Start() install exception ports
        // before anything resumes it.
        mach_port_t task = MACH_PORT_NULL;
        const kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
        mLastTaskForPidResult = kr;
        if(kr != KERN_SUCCESS)
        {
            cbInternalError("task_for_pid failed for pid " + std::to_string(pid) + ": " +
                             Process::DescribeKernReturn(kr));

            // Unlike Init()'s equivalent failure, pid did not come from this engine -- it was
            // already running when Attach() was called, and a caller reaching for Attach() does
            // not get to have it killed just because this engine could not finish taking control
            // of it. Detach and resume it exactly as if this call had never happened, rather than
            // leaving someone else's process parked in a job-control stop it never asked for.
            ptrace(PT_DETACH, pid, reinterpret_cast<caddr_t>(1), 0);
            return false;
        }

        mProcess = std::make_unique<Process>(pid, task);
        mAttachedViaPtrace = true;
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

    kern_return_t Debugger::LastTaskForPidResult() const
    {
        return mLastTaskForPidResult;
    }

    bool Debugger::Terminate()
    {
        if(!mProcess)
            return false;

        const pid_t pid = mProcess->pid;
        mProcess.reset();
        mAttachedViaPtrace = false;

        // Process::DetachAndKill(), not a bare kill()+waitpid(): if Start() ran, or this pid was
        // taken by Attach() (which ptrace(PT_ATTACHEXC)'s it directly, without waiting for
        // Start()), this pid is ptrace(PT_ATTACHEXC)'d (Debugger.Loop.cpp::Start(), or Attach()
        // above) -- possibly still, if Start()'s own loop ended some other way than Stop() (an
        // unexpected mach_msg failure, say) without ever tearing that down. A plain SIGKILL sent
        // to a still-attached pid is routed through its Mach exception port exactly like any
        // other signal, and with nothing left to service that port, is never answered -- the pid
        // does not die, at all, not even slower. See that helper's comment in Process.h for the
        // measured failure mode this once was.
        return Process::DetachAndKill(pid, nullptr);
    }

    void Debugger::cbInternalError(const std::string & error) { (void)error; }
}
