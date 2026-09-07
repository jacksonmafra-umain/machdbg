#include <ElfBug/core/Debugger.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#include <csignal>

namespace ElfBug
{
    void Debugger::handleSignal(const pid_t pid, const int status)
    {
        const int sig = WSTOPSIG(status);
        mPendingSignal = 0;

        {
            std::unique_lock lock(mProcessMutex);
            mThread = nullptr;
            if(mProcess)
            {
                const auto it = mProcess->threads.find(pid);
                if(it != mProcess->threads.end())
                    mThread = it->second.get();
            }
        }

        switch(sig)
        {
        case SIGTRAP:
            handleSigtrap(pid, status);
            break;

        case SIGSTOP:
        {
            if(mPauseRequested.load(std::memory_order_acquire))
            {
                mPauseRequested.store(false, std::memory_order_release);
                // TODO: all-stop mode - send tgkill(mMainPid, tid, SIGSTOP)
                // to every other thread and waitpid each
                if(mThread)
                {
                    mThread->registers.Read();
                    beginPause();
                    cbPaused();

                    if(!pauseAndResume(pid))
                        break;
                }
                else
                {
                    if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                        cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
                }
            }
            else
            {
                if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                    cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            }
            break;
        }

        default:
        {
            ptr faultAddr = 0;
            siginfo_t sigInfo;
            if(ptrace(PTRACE_GETSIGINFO, pid, nullptr, &sigInfo) != -1)
                faultAddr = reinterpret_cast<ptr>(sigInfo.si_addr);
            if(mThread)
            {
                mThread->registers.Read();
                mPendingSignal = sig;
                beginPause();
                cbExceptionEvent(sig, faultAddr);
                if(!pauseAndResume(pid))
                    break;
            }
            else
            {
                cbExceptionEvent(sig, faultAddr);
                if(ptrace(PTRACE_CONT, pid, nullptr,
                          reinterpret_cast<void*>(static_cast<uintptr_t>(sig))) == -1)
                    cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            }
            break;
        }
        }
    }

    void Debugger::handleSigtrap(const pid_t pid, const int status)
    {
        const int event = (status >> 16) & 0xffff;

        switch(event)
        {
        case PTRACE_EVENT_EXEC:
        {
            // TODO: re-exec handling - re-detect arch and reject if no longer x86_64, clear breakpoints, refresh memory map, fire callback
            if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            break;
        }

        case PTRACE_EVENT_CLONE:
        {
            unsigned long newTid = 0;
            if(ptrace(PTRACE_GETEVENTMSG, pid, nullptr, &newTid) == -1)
            {
                cbInternalError("PTRACE_GETEVENTMSG failed: " + std::string(strerror(errno)));
            }
            else
            {
                createThreadEvent(static_cast<pid_t>(newTid));
            }
            if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            break;
        }

        case PTRACE_EVENT_EXIT:
        {
            // Notification only; exit is emitted via WIFEXITED/WIFSIGNALED in debugLoop.
            if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            break;
        }

        default:
        {
            if(!mThread)
            {
                if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                    cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
                break;
            }

            mThread->registers.Read();

            if(mThread->isSingleStepping())
            {
                mThread->clearSingleStep();
                beginPause();
                cbStep();
                if(!pauseAndResume(pid))
                    break;
                break;
            }

            if(mProcess)
            {
                ptr bpAddr = mThread->registers.Gip() - 1;
                const BreakpointKey key{BreakpointType::Software, bpAddr};
                const auto it = mProcess->breakpoints.find(key);

                if(it != mProcess->breakpoints.end())
                {
                    mThread->registers.Gip() = bpAddr;
                    mThread->registers.Write();

                    const auto & info = it->second;
                    const bool singleshot = info.singleshot;

                    beginPause();

                    const auto cbIt = mProcess->breakpointCallbacks.find(key);
                    if(cbIt != mProcess->breakpointCallbacks.end())
                        cbIt->second(info);

                    cbBreakpoint(info);

                    if(singleshot)
                    {
                        mProcess->DeleteBreakpoint(bpAddr);
                    }
                    else
                    {
                        mProcess->DeleteBreakpoint(bpAddr);
                        if(!mThread->StepInto())
                        {
                            cbInternalError("PTRACE_SINGLESTEP failed: " + std::string(strerror(errno)));
                            mProcess->SetBreakpoint(bpAddr, false);
                            if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                                cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
                            break;
                        }
                        int stepStatus = 0;
                        pid_t waited = -1;
                        do
                        {
                            waited = waitpid(pid, &stepStatus, __WALL);
                        }
                        while(waited == -1 && errno == EINTR);
                        if(waited == -1)
                        {
                            cbInternalError("waitpid(step) failed: " + std::string(strerror(errno)));
                            mProcess->SetBreakpoint(bpAddr, false);
                            break;
                        }
                        mThread->clearSingleStep();

                        if(WIFEXITED(stepStatus) || WIFSIGNALED(stepStatus))
                        {
                            const int code = WIFEXITED(stepStatus)
                                             ? WEXITSTATUS(stepStatus)
                                             : -WTERMSIG(stepStatus);
                            if(pid == mMainPid.load(std::memory_order_relaxed))
                            {
                                exitProcessEvent(pid, code);
                                mIsRunning.store(false, std::memory_order_release);
                            }
                            else
                            {
                                exitThreadEvent(pid);
                            }
                            break;
                        }

                        if(WIFSTOPPED(stepStatus))
                        {
                            const int stepSig = WSTOPSIG(stepStatus);
                            mThread->registers.Read();
                            mProcess->SetBreakpoint(bpAddr, false);

                            if(stepSig != SIGTRAP)
                            {
                                if(ptrace(PTRACE_CONT, pid, nullptr,
                                          reinterpret_cast<void*>(static_cast<uintptr_t>(stepSig))) == -1)
                                    cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
                                break;
                            }
                        }
                    }

                    if(!pauseAndResume(pid))
                        break;
                    break;
                }
            }

            if(ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1)
                cbInternalError("PTRACE_CONT failed: " + std::string(strerror(errno)));
            break;
        }
        }
    }
}
