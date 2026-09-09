#pragma once

#include <MachBug/core/Debugger.h>
#include <MachBug/types/MachBug.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace MachBug::test
{
    // Mirrors ElfBug/tests/TestHarness.h's shape: an EventType enum, a timestamped Event, a
    // RecordingDebugger subclassing the engine's Debugger and overriding its virtual callbacks
    // to append to a timeline, StartOnThread(), and a WaitFor() that blocks on a condition
    // variable with a timeout. Read that file first if this one is confusing; it is not trying
    // to be original.
    //
    // One deliberate divergence from it: ElfBug's WaitFor() throws on timeout. This one returns
    // false. A test that already REQUIREs the result gets a normal, fast, named CI failure
    // ("WaitFor timed out" style REQUIRE) either way, but a bool return also lets a caller probe
    // for an event optionally (without a try/catch) -- and Task 4's temporary LocalRecordingDebugger,
    // which this replaces, already established the bool convention for MachBug's tests. Matching
    // it here means Task 4's call sites (`REQUIRE(debugger.WaitFor(...))`) need no change beyond
    // the type name.
    //
    // The EventType set below is almost a SUBSET of ElfBug's, and deliberately so. MachBug::
    // Debugger (core/Debugger.h) exposes seven overridable callbacks: cbInternalError,
    // cbCreateProcessEvent, cbExitProcessEvent, cbSystemBreakpoint, cbStep, cbException, and
    // cbResumed -- all but the last already have a same-named counterpart in ElfBug's EventType
    // (CreateProcess, ExitProcess, SystemBreakpoint, Step, Exception, InternalError). Resumed is
    // the one MachBug-only slot, and it exists because MachBug's stopped state is an unanswered
    // exception reply rather than a kernel-level stop: only the engine knows when that reply has
    // gone out, so only the engine can tell a test the target is running again (see cbResumed()
    // in Debugger.h). ElfBug needs no equivalent -- a tracee resumes when ptrace(PTRACE_CONT)
    // returns, on the caller's own thread.
    // What ElfBug has and this file does not add -- CreateThread, ExitThread, Breakpoint, Paused
    // -- all name capabilities (multi-threaded tracking, breakpoint dispatch, an explicit "now
    // paused" notification) that milestone 2's Debugger does not implement yet, not events this
    // harness forgot to wire up:
    //   - CreateThread/ExitThread: Debugger.h tracks a single thread implicitly (see its own
    //     comment on holding one unique_ptr<Process> rather than ElfBug's map); there is no
    //     per-thread lifecycle callback to record yet.
    //   - Breakpoint: filled in by milestone 4, which added the table that makes the match
    //     possible. Its Event carries the breakpoint's own address in `address` -- the address
    //     the user asked for, after the architecture's program-counter fixup, not the raw pc the
    //     exception arrived with.
    //   - Paused: Pause() (Debugger.h) is synchronous -- task_suspend() has already happened by
    //     the time it returns -- so there is no asynchronous "now paused" notification to wait
    //     on; a caller checks IsStopped() (inherited straight from Debugger, unchanged by this
    //     harness) immediately after Pause() returns instead of waiting on an event that would
    //     never arrive.
    // Adding placeholder EventTypes nothing would ever push felt worse than this comment; extend
    // the enum in whichever later milestone adds the callback it would correspond to.
    enum class EventType
    {
        CreateProcess,
        ExitProcess,
        SystemBreakpoint,
        Resumed,
        Step,
        Breakpoint,
        Exception,
        InternalError,
    };

    struct Event
    {
        EventType type{};
        std::chrono::steady_clock::time_point when{};
        pid_t pid = 0;
        int exitCode = 0;
        // Populated only for EventType::Exception, straight from cbException(type, address):
        // exceptionType is the raw Mach exception_type_t (EXC_BREAKPOINT, EXC_BAD_ACCESS, ...),
        // not a Unix signal number -- MachBug has no signal to report the way ElfBug's
        // cbExceptionEvent(signal, address) does, so this field is named for what it actually
        // is rather than reusing ElfBug's field name for something different.
        uint32_t exceptionType = 0;
        uint64_t address = 0;
        std::string message;
    };

    class RecordingDebugger : public Debugger
    {
    public:
        // Same ordering requirement ~Debugger() documents on itself: if Start() is still running
        // on mLoopThread when a derived destructor runs, that thread must already have returned
        // before ~Debugger() tears down mProcess. Stop() + a *bounded* wait is what this tries to
        // guarantee -- not a bare join(), which would turn a genuine regression in Stop()'s own
        // teardown (exactly the bug Task 4's "Stop() kills a genuinely running target" test
        // guards against) into a hung test binary instead of one failing test.
        //
        // But the bounded wait can still time out, and what happens next matters: the instant
        // this destructor returns, ~Debugger() runs unconditionally and calls Terminate(), which
        // does `mProcess.reset()` -- destroying the Process object (and, via ~Process(),
        // deallocating its Mach task port) whether or not mLoopThread has actually stopped
        // touching it. If exceptionLoop() is still running on mLoopThread at that point (which is
        // exactly what a timed-out wait means), it is still reading and writing that same
        // mProcess -- so continuing on to ~Debugger() is a genuine use-after-free race, not a
        // harmless leak. An earlier version of this destructor detached mLoopThread and let
        // ~Debugger() proceed anyway on the theory that a detached thread "leaks harmlessly";
        // that was wrong -- detaching only stops *this* class from waiting on the thread, it does
        // nothing to stop ~Debugger() from destroying what that detached thread still reads.
        //
        // Neither this class nor core/Debugger.h (owned by a concurrent task -- see this file's
        // header comment -- and not modified here) gives a derived destructor any way to skip or
        // delay ~Debugger()'s own teardown once this function returns: C++ runs a derived
        // destructor's base subobject destructor unconditionally right after, whether the derived
        // one returns normally or throws. The only way left, from tests/, to make sure Terminate()
        // never touches mProcess while mLoopThread might still be using it is to never reach that
        // return at all: abort() before it. That is a deliberate escape hatch, not a shortcut --
        // it fires in exactly one circumstance (Stop() failing to bring the loop down), which is
        // precisely the class of regression this harness exists to catch, and aborting there turns
        // what would otherwise be a silent corruption (surfacing, if at all, as a bewildering crash
        // in some unrelated later test) into an immediate, loud, unmistakably-attributed failure --
        // Catch2's own fatal-condition handler additionally reports which test case was running
        // when it happened, on top of the message below naming the cause directly.
        ~RecordingDebugger() override
        {
            Stop();
            if(!mLoopThread.joinable())
                return;

            if(!WaitForLoopToFinish())
            {
                std::fprintf(stderr,
                    "\nRecordingDebugger: Stop() did not bring the exception loop down within the "
                    "timeout.\nThe loop thread is still running and still touching state "
                    "~Debugger() is about to\ndestroy (Terminate() -> mProcess.reset() -> "
                    "~Process() -> mach_port_deallocate);\ncontinuing would be a use-after-free "
                    "race, not a harmless leak, so this process is\naborting instead of letting "
                    "that happen. This means Stop() itself has regressed --\nlook at "
                    "Debugger::Stop()/exceptionLoop()'s teardown, not at whatever test happens\n"
                    "to run (or crash) next.\n\n");
                std::fflush(stderr);
                std::abort();
            }

            mLoopThread.join();
        }

        void StartOnThread()
        {
            mLoopThread = std::thread([this] {
                Start();
                {
                    std::lock_guard lock(mMutex);
                    mLoopFinished = true;
                }
                mCv.notify_all();
            });
        }

        // Bounded wait for Start() (running via StartOnThread()) to have actually returned.
        // There is no timed std::thread::join(), so this is what a caller (including this
        // class's own destructor) uses instead of risking a bare join() hanging forever on a
        // regression -- see the destructor's comment.
        bool WaitForLoopToFinish(std::chrono::milliseconds timeout = std::chrono::seconds(5))
        {
            std::unique_lock lock(mMutex);
            return mCv.wait_for(lock, timeout, [this] { return mLoopFinished; });
        }

        // Blocks until an event of `type` is published or `timeout` elapses, returning false in
        // the latter case rather than hanging. Consider carefully what a harness's WaitFor should
        // do on timeout: hanging makes a CI run time out with no indication of what was expected
        // and never happened; returning false lets the caller's own REQUIRE/INFO say exactly
        // that, and fail in seconds instead of at the job's outer timeout. A deadlock that reads
        // as an ordinary, fast failure is far cheaper to diagnose than one that reads as a stuck
        // job -- Task 4 hardened its temporary recorder for exactly this reason, and this harness
        // keeps that property rather than regressing to a bare, unbounded wait.
        //
        // Each successful WaitFor() consumes events up to and including the match (mConsumedUpto),
        // the same way ElfBug's harness does, so a test that waits for SystemBreakpoint and then
        // for ExitProcess is waiting for the *next* occurrence of each, in order, rather than
        // matching the same already-seen event twice. events() below is unaffected by this and
        // always returns the complete timeline.
        bool WaitFor(EventType type, std::chrono::milliseconds timeout = std::chrono::seconds(5))
        {
            std::unique_lock lock(mMutex);
            const auto start = std::chrono::steady_clock::now();
            while(true)
            {
                for(std::size_t i = mConsumedUpto; i < mEvents.size(); ++i)
                {
                    if(mEvents[i].type == type)
                    {
                        mConsumedUpto = i + 1;
                        return true;
                    }
                }

                const auto elapsed = std::chrono::steady_clock::now() - start;
                if(elapsed >= timeout)
                    return false;
                mCv.wait_for(lock, timeout - elapsed);
            }
        }

        // The full event timeline recorded so far, in publish order. Use this (rather than
        // WaitFor()'s return value, which is a plain bool) to assert on ordering, timestamps, or
        // per-event fields such as exitCode/address/message.
        std::vector<Event> events() const
        {
            std::lock_guard lock(mMutex);
            return mEvents;
        }

        std::size_t count(EventType type) const
        {
            std::lock_guard lock(mMutex);
            return static_cast<std::size_t>(
                std::count_if(mEvents.begin(), mEvents.end(),
                              [type](const Event & e) { return e.type == type; }));
        }

        // Set by cbExitProcessEvent(); -1 (never a real exit code) until then.
        int LastExitCode() const
        {
            std::lock_guard lock(mMutex);
            return mExitCode;
        }

        // IsStopped() and IsRunning() are inherited straight from Debugger, unchanged: neither
        // needs a recorded event to answer (see this file's header comment on why there is no
        // EventType::Paused), and Task 4's tests call IsStopped() directly on a RecordingDebugger
        // instance for exactly that reason.

    protected:
        void cbInternalError(const std::string & error) override
        {
            push({EventType::InternalError, {}, 0, 0, 0, 0, error});
        }

        void cbCreateProcessEvent(pid_t pid) override
        {
            push({EventType::CreateProcess, {}, pid, 0, 0, 0, {}});
        }

        void cbExitProcessEvent(int exitCode) override
        {
            {
                std::lock_guard lock(mMutex);
                mExitCode = exitCode;
            }
            push({EventType::ExitProcess, {}, 0, exitCode, 0, 0, {}});
        }

        void cbSystemBreakpoint() override
        {
            push({EventType::SystemBreakpoint, {}, 0, 0, 0, 0, {}});
        }

        void cbResumed() override
        {
            push({EventType::Resumed, {}, 0, 0, 0, 0, {}});
        }

        void cbBreakpoint(uint64_t address) override
        {
            push({EventType::Breakpoint, {}, 0, 0, 0, address, {}});
        }

        void cbStep() override
        {
            push({EventType::Step, {}, 0, 0, 0, 0, {}});
        }

        void cbException(uint32_t type, uint64_t address) override
        {
            push({EventType::Exception, {}, 0, 0, type, address, {}});
        }

    private:
        void push(Event e)
        {
            e.when = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(mMutex);
                mEvents.push_back(std::move(e));
            }
            mCv.notify_all();
        }

        mutable std::mutex mMutex;
        std::condition_variable mCv;
        std::vector<Event> mEvents;
        std::size_t mConsumedUpto = 0;
        bool mLoopFinished = false;
        int mExitCode = -1;
        std::thread mLoopThread;
    };
}
