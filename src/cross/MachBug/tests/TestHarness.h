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
#include <unistd.h>
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
        ThreadCreate,
        ThreadExit,
        LoadModule,
        UnloadModule,
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
        // Populated only for ThreadCreate/ThreadExit: the id the engine reports, which is a Mach
        // thread port and is what ResolveThread() accepts.
        uint64_t threadId = 0;
        std::string message;
        // Populated only for LoadModule/UnloadModule: the image's load address, with its path in
        // `message` for a load. Last in the struct deliberately -- the pushes above initialise
        // Event positionally, and a field added in the middle would silently shift every one of
        // them.
        uint64_t moduleBase = 0;
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
                    "timeout; the loop reports it is %s.", TeardownStageName());
                std::fprintf(stderr,
                    "\n\nThe loop thread is still running and still touching state "
                    "~Debugger() is about to\ndestroy (Terminate() -> mProcess.reset() -> "
                    "~Process() -> mach_port_deallocate);\ncontinuing would be a use-after-free "
                    "race, not a harmless leak, so this process is\naborting instead of letting "
                    "that happen. Something in Stop()/exceptionLoop()'s teardown is not "
                    "finishing --\nthe stage named above says which part, so look there rather "
                    "than at whatever test\nhappens to run (or crash) next.\n\n");
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
        // Fifteen seconds, not five, and the number is not arbitrary: Process::WaitForRealExit
        // polls for up to ten (500 attempts, 20ms apart) before giving up on a target that will
        // not die. A guard tighter than the thing it guards fires on a teardown that was working
        // -- which is what it did, aborting a passing test on the Intel runner and once in a
        // dozen local runs, with a message blaming a Stop() that had not regressed at all. This
        // bound must stay strictly greater than the engine's own.
        bool WaitForLoopToFinish(std::chrono::milliseconds timeout = std::chrono::seconds(15))
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
            push({EventType::InternalError, {}, 0, 0, 0, 0, 0, error});
        }

        void cbCreateProcessEvent(pid_t pid) override
        {
            push({EventType::CreateProcess, {}, pid, 0, 0, 0, 0, {}});
        }

        void cbExitProcessEvent(int exitCode) override
        {
            {
                std::lock_guard lock(mMutex);
                mExitCode = exitCode;
            }
            push({EventType::ExitProcess, {}, 0, exitCode, 0, 0, 0, {}});
        }

        void cbSystemBreakpoint() override
        {
            push({EventType::SystemBreakpoint, {}, 0, 0, 0, 0, 0, {}});
        }

        void cbResumed() override
        {
            push({EventType::Resumed, {}, 0, 0, 0, 0, 0, {}});
        }

        void cbBreakpoint(uint64_t address) override
        {
            push({EventType::Breakpoint, {}, 0, 0, 0, address, 0, {}});
        }

        void cbLoadModule(uint64_t base, const std::string& path) override
        {
            Event event{EventType::LoadModule};
            event.moduleBase = base;
            event.message = path;
            push(std::move(event));
        }

        void cbUnloadModule(uint64_t base) override
        {
            Event event{EventType::UnloadModule};
            event.moduleBase = base;
            push(std::move(event));
        }

        void cbThreadCreate(uint64_t threadId) override
        {
            push({EventType::ThreadCreate, {}, 0, 0, 0, 0, threadId, {}});
        }

        void cbThreadExit(uint64_t threadId) override
        {
            push({EventType::ThreadExit, {}, 0, 0, 0, 0, threadId, {}});
        }

        void cbStep() override
        {
            push({EventType::Step, {}, 0, 0, 0, 0, 0, {}});
        }

        void cbException(uint32_t type, uint64_t address) override
        {
            push({EventType::Exception, {}, 0, 0, type, address, 0, {}});
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

    // Launches a fixture that prints one hexadecimal address on stdout, reads that address, and
    // leaves the target stopped with it in hand. Two fixtures publish an address that way
    // (known_function, writes_a_global) and every breakpoint test needs one, so the capture
    // lives here rather than being copied per test file.
    //
    // The capture technique: the fixture inherits fd 1 at spawn time, so this process's stdout
    // is redirected for Init() and restored immediately, and nothing is asserted while it is
    // redirected -- Catch2's -s trace would otherwise land in the capture file and corrupt the
    // very line being read.
    inline uint64_t LaunchAndReadPublishedAddress(RecordingDebugger& debugger,
                                                  const std::string& fixturePath)
    {
        char outPathTemplate[] = "/tmp/machbug_published_address.XXXXXX";
        const int outFd = mkstemp(outPathTemplate);
        if(outFd == -1)
            return 0;
        const std::string outPath = outPathTemplate;

        const int savedStdout = dup(STDOUT_FILENO);
        if(savedStdout == -1)
            return 0;

        std::fflush(stdout);
        const int redirectRc = dup2(outFd, STDOUT_FILENO);
        const bool launched = redirectRc != -1 && debugger.Init(fixturePath.c_str());
        const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
        close(savedStdout);

        if(!launched || restoreRc == -1)
        {
            close(outFd);
            unlink(outPath.c_str());
            return 0;
        }

        debugger.StartOnThread();
        if(!debugger.WaitFor(EventType::SystemBreakpoint))
        {
            close(outFd);
            unlink(outPath.c_str());
            return 0;
        }

        // Resumed so the fixture reaches its printf, then paused again: a debugger writes into a
        // stopped target, which is the only state its breakpoints are installed in anyway.
        debugger.Continue();
        debugger.WaitFor(EventType::Resumed);

        uint64_t address = 0;
        for(int attempt = 0; attempt < 500 && address == 0; ++attempt)
        {
            if(FILE* captured = std::fopen(outPath.c_str(), "r"))
            {
                unsigned long long printed = 0;
                if(std::fscanf(captured, "%llx", &printed) == 1)
                    address = printed;
                std::fclose(captured);
            }
            if(address == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        close(outFd);
        unlink(outPath.c_str());

        debugger.Pause();
        return address;
    }
}
