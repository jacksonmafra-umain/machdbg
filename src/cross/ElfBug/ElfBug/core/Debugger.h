#pragma once

#include <sys/types.h>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <string>
#include <unordered_map>
#include <vector>
#include <ElfBug/types/ElfBug.h>
#include <ElfBug/types/Global.h>
#include <ElfBug/process/Process.h>
#include <ElfBug/thread/Thread.h>

namespace ElfBug
{
    class Debugger
    {
    public:
        Debugger();
        virtual ~Debugger();

        bool Init(const char* szFilePath, const char* const* argv = nullptr, const char* szCurrentDirectory = nullptr);

        bool Attach(pid_t processId);
        void Start();
        void Continue();
        void StepInto();
        void Pause();
        bool Stop();
        void Detach();

    protected:
        virtual void cbCreateProcessEvent(pid_t pid, ptr entryPoint);
        virtual void cbExitProcessEvent(int exitCode);
        virtual void cbCreateThreadEvent(pid_t tid);
        virtual void cbExitThreadEvent(pid_t tid);
        virtual void cbLoadDllEvent(ptr baseAddress, const std::string & path);
        virtual void cbUnloadDllEvent(ptr baseAddress);
        virtual void cbExceptionEvent(int signal, ptr address);
        virtual void cbBreakpoint(const BreakpointInfo & info);
        virtual void cbStep();
        virtual void cbSystemBreakpoint();
        virtual void cbAttachBreakpoint();
        virtual void cbUnhandledException(int signal, ptr address);
        virtual void cbInternalError(const std::string & error);
        virtual void cbDebugStringEvent(const std::string & text);
        virtual void cbPaused(); // called when the debuggee is paused by user
        virtual void cbPauseTick(); // called each iteration of the pause spin loop

        Process* mProcess = nullptr;
        Thread* mThread = nullptr;
        std::unordered_map<pid_t, Process> mProcesses;
        mutable std::shared_mutex mProcessMutex;

    private:
        void debugLoop();
        bool launchChild();
        void handleSignal(pid_t pid, int status);
        void handleSigtrap(pid_t pid, int status);
        bool pauseAndResume(pid_t pid);
        void beginPause();
        void createProcessEvent(pid_t pid, Arch arch);
        void exitProcessEvent(pid_t pid, int exitCode);
        void createThreadEvent(pid_t tid);
        void exitThreadEvent(pid_t tid);

        // Tracer-thread only; caller threads must not write.
        std::atomic<bool> mIsRunning{false};
        std::atomic<bool> mPaused{false};
        std::atomic<bool> mStepPending{false};
        std::atomic<bool> mPauseRequested{false};
        std::atomic<pid_t> mMainPid{0};
        int mPendingSignal = 0;
        std::mutex mPauseMutex;
        std::condition_variable mPauseCv;

        std::string mFilePath;
        std::vector<std::string> mArgv;
        std::string mCwd;
        bool mHasLaunchArgs = false;
    };
}
