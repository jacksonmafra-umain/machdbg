#include <ElfBug/core/Debugger.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/personality.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <csignal>
#include <cstring>

namespace ElfBug
{
    Debugger::Debugger() = default;

    Debugger::~Debugger()
    {
        const pid_t pid = mMainPid.load(std::memory_order_acquire);
        if(pid > 0)
        {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, __WALL);
        }
    }

    bool Debugger::Init(const char* szFilePath, const char* const* argv, const char* szCurrentDirectory)
    {
        mHasLaunchArgs = false;
        mFilePath.clear();
        mArgv.clear();
        mCwd.clear();

        mPaused.store(false, std::memory_order_release);
        mStepPending.store(false, std::memory_order_release);
        mPauseRequested.store(false, std::memory_order_release);
        mPendingSignal = 0;

        if(!szFilePath)
            return false;

        if(!szCurrentDirectory && access(szFilePath, X_OK) != 0)
        {
            cbInternalError("cannot execute '" + std::string(szFilePath) + "': " + std::string(strerror(errno)));
            return false;
        }

        mFilePath = szFilePath;
        mCwd = szCurrentDirectory ? szCurrentDirectory : "";
        if(argv)
        {
            for(const char* const* p = argv; *p != nullptr; ++p)
                mArgv.emplace_back(*p);
        }
        mHasLaunchArgs = true;
        return true;
    }

    bool Debugger::launchChild()
    {
        if(!mHasLaunchArgs)
        {
            cbInternalError("launchChild called without Init");
            return false;
        }

        int pipeFds[2];
        if(pipe2(pipeFds, O_CLOEXEC) == -1)
        {
            cbInternalError("pipe2() failed: " + std::string(strerror(errno)));
            return false;
        }

        const pid_t pid = fork();
        if(pid == -1)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            cbInternalError("fork() failed: " + std::string(strerror(errno)));
            return false;
        }

        if(pid == 0)
        {
            close(pipeFds[0]);

            auto childError = [&](const char* msg)
            {
                write(pipeFds[1], msg, strlen(msg));
                _exit(1);
            };

            if(setpgid(0, 0) < 0)
                childError("setpgid failed");

            if(!mCwd.empty())
            {
                if(chdir(mCwd.c_str()) == -1)
                    childError("chdir failed");
            }

            if(personality(ADDR_NO_RANDOMIZE) == -1)
                childError("personality(ADDR_NO_RANDOMIZE) failed");

            if(ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1)
                childError("PTRACE_TRACEME failed");

            std::vector<char*> argvPtrs;
            if(!mArgv.empty())
            {
                argvPtrs.reserve(mArgv.size() + 1);
                for(auto & s : mArgv)
                    argvPtrs.push_back(s.data());
                argvPtrs.push_back(nullptr);
                execv(mFilePath.c_str(), argvPtrs.data());
            }
            else
            {
                char* defaultArgv[] = { const_cast<char*>(mFilePath.c_str()), nullptr };
                execv(mFilePath.c_str(), defaultArgv);
            }

            childError("execv failed");
        }

        close(pipeFds[1]);

        char errBuf[256] = {};
        ssize_t n;
        do
        {
            n = read(pipeFds[0], errBuf, sizeof(errBuf) - 1);
        }
        while(n == -1 && errno == EINTR);
        close(pipeFds[0]);

        if(n == -1)
        {
            const std::string err = strerror(errno);
            waitpid(pid, nullptr, 0);
            cbInternalError("read() from child pipe failed: " + err);
            return false;
        }

        if(n > 0)
        {
            waitpid(pid, nullptr, 0);
            cbInternalError("child process failed: " + std::string(errBuf));
            return false;
        }

        mMainPid.store(pid, std::memory_order_release);
        return true;
    }

    bool Debugger::Attach(pid_t)
    {
        // TODO: implement ptrace attach
        cbInternalError("Attach not implemented");
        return false;
    }

    void Debugger::Start()
    {
        mIsRunning.store(true, std::memory_order_release);
        debugLoop();
    }

    void Debugger::Continue()
    {
        {
            std::lock_guard lock(mPauseMutex);
            mPaused.store(false, std::memory_order_release);
        }
        mPauseCv.notify_one();
    }

    void Debugger::StepInto()
    {
        {
            std::lock_guard lock(mPauseMutex);
            mStepPending.store(true, std::memory_order_release);
            mPaused.store(false, std::memory_order_release);
        }
        mPauseCv.notify_one();
    }

    void Debugger::Pause()
    {
        const pid_t pid = mMainPid.load(std::memory_order_acquire);
        if(pid > 0)
        {
            mPauseRequested.store(true, std::memory_order_release);
            kill(pid, SIGSTOP);
        }
    }

    bool Debugger::Stop()
    {
        const pid_t pid = mMainPid.load(std::memory_order_acquire);
        if(pid <= 0)
            return false;

        {
            std::lock_guard lock(mPauseMutex);
            mPaused.store(false, std::memory_order_release);
        }
        mPauseCv.notify_one();

        return kill(pid, SIGKILL) == 0;
    }

    void Debugger::Detach()
    {
        // TODO: implement ptrace detach
        cbInternalError("Detach not implemented");
    }

    void Debugger::cbCreateProcessEvent(const pid_t pid, const ptr entryPoint) { (void)pid; (void)entryPoint; }
    void Debugger::cbExitProcessEvent(const int exitCode) { (void)exitCode; }
    void Debugger::cbCreateThreadEvent(const pid_t tid) { (void)tid; }
    void Debugger::cbExitThreadEvent(const pid_t tid) { (void)tid; }
    void Debugger::cbLoadDllEvent(const ptr baseAddress, const std::string & path) { (void)baseAddress; (void)path; }
    void Debugger::cbUnloadDllEvent(const ptr baseAddress) { (void)baseAddress; }
    void Debugger::cbExceptionEvent(const int signal, const ptr address) { (void)signal; (void)address; }
    void Debugger::cbBreakpoint(const BreakpointInfo & info) { (void)info; }
    void Debugger::cbStep() {}
    void Debugger::cbSystemBreakpoint() {}
    void Debugger::cbAttachBreakpoint() {}
    void Debugger::cbUnhandledException(const int signal, const ptr address) { (void)signal; (void)address; }
    void Debugger::cbInternalError(const std::string & error) { (void)error; }
    void Debugger::cbDebugStringEvent(const std::string & text) { (void)text; }
    void Debugger::cbPaused() {}
    void Debugger::cbPauseTick() {}
}
