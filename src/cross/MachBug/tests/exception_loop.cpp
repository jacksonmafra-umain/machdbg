#include <catch2/catch_test_macros.hpp>
#include <MachBug/core/Debugger.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <queue>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

namespace
{
    long fileSize(const std::string & path)
    {
        struct stat st{};
        if(stat(path.c_str(), &st) != 0)
            return -1;
        return static_cast<long>(st.st_size);
    }

    // TEMPORARY, task 5 replaces this: the task 4 brief anticipates that the real recording
    // harness (MachBug::test::RecordingDebugger, mirroring ElfBug/tests/TestHarness.h) is task
    // 5's own deliverable and would not exist yet when this test is written. Rather than commit
    // a test that fails to compile until task 5 lands, this file carries its own minimal
    // recorder -- deliberately NOT named MachBug::test::RecordingDebugger and NOT living in a
    // shared header, so task 5 can add the real one without colliding with this file. Delete
    // LocalRecordingDebugger (and switch these TEST_CASEs to the real harness) once task 5 lands
    // instead of trying to reconcile the two.
    enum class EventType
    {
        SystemBreakpoint,
        ExitProcess,
    };

    class LocalRecordingDebugger : public MachBug::Debugger
    {
    public:
        ~LocalRecordingDebugger() override
        {
            // Stop() + join() must happen here, in the derived destructor, before ~Debugger()
            // (which calls Terminate()) runs -- see the ordering note on ~Debugger() in
            // Debugger.h. By the time the base destructor runs, mLoopThread is guaranteed to
            // have already returned from Start(), so Terminate() never races the loop thread's
            // own use of mProcess.
            Stop();
            if(mLoopThread.joinable())
                mLoopThread.join();
        }

        bool StartOnThread()
        {
            mLoopThread = std::thread([this] { Start(); });
            return true;
        }

        bool WaitFor(const EventType type,
                     const std::chrono::milliseconds timeout = std::chrono::seconds(5))
        {
            std::unique_lock<std::mutex> lock(mEventMutex);
            return mEventCv.wait_for(lock, timeout, [&] {
                while(!mEvents.empty())
                {
                    const EventType next = mEvents.front();
                    mEvents.pop();
                    if(next == type)
                        return true;
                }
                return false;
            });
        }

        int LastExitCode() const { return mExitCode; }

    protected:
        void cbSystemBreakpoint() override { pushEvent(EventType::SystemBreakpoint); }

        void cbExitProcessEvent(int exitCode) override
        {
            mExitCode = exitCode;
            pushEvent(EventType::ExitProcess);
        }

    private:
        void pushEvent(const EventType type)
        {
            std::lock_guard<std::mutex> lock(mEventMutex);
            mEvents.push(type);
            mEventCv.notify_all();
        }

        std::thread mLoopThread;
        std::mutex mEventMutex;
        std::condition_variable mEventCv;
        std::queue<EventType> mEvents;
        int mExitCode = -1;
    };
}

// This is the test the task brief asks for verbatim (Step 2), adapted to LocalRecordingDebugger
// -- see its comment for why.
TEST_CASE("the first exception stops the target until it is resumed")
{
    LocalRecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("hello_machbug").c_str()));
    REQUIRE(debugger.StartOnThread());

    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));
    REQUIRE(debugger.IsStopped());

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::ExitProcess));
    REQUIRE(debugger.LastExitCode() == 0);
}

// Step 7 of the task brief: proving the stop is real rather than merely slow. Same technique as
// debugger_launch.cpp's "a suspended launch produces no output until resumed" (redirect
// hello_machbug's stdout to a file and sample its size), but exercising the real exception-loop
// Continue() instead of a bare SIGCONT -- this is the regression test for the loop itself, not
// for Init()'s suspended launch.
TEST_CASE("the target does not run while stopped at the first exception, and does once resumed")
{
    char outPathTemplate[] = "/tmp/machbug_exception_loop_stop_test.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;

    // hello_machbug inherits fd 1 at spawn time, so this process's own stdout is redirected to
    // the capture file just for the launch and the stopped window, then restored before any
    // assertion below -- same reasoning, and same non-asserted dup2() bare statements, as
    // debugger_launch.cpp's parallel test: a Catch2 -s trace of a passing assertion must never
    // land inside the capture file it would then corrupt.
    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);
    std::fflush(stdout);
    const int redirectRc = dup2(outFd, STDOUT_FILENO);

    LocalRecordingDebugger debugger;
    const bool launched = redirectRc != -1 && debugger.Init(FIXTURE("hello_machbug").c_str());
    const bool started = launched && debugger.StartOnThread();
    const bool stoppedAtFirstException = started && debugger.WaitFor(EventType::SystemBreakpoint);

    const long sizeAtStop = stoppedAtFirstException ? fileSize(outPath) : -1;

    bool stillStoppedAfterWait = false;
    long sizeAfterWait = -1;
    if(stoppedAtFirstException)
    {
        // A second is affordable for one test run, and long enough that a merely slow stop
        // (rather than a truly withheld reply) would have let hello_machbug print by now.
        std::this_thread::sleep_for(std::chrono::seconds(1));
        stillStoppedAfterWait = debugger.IsStopped();
        sizeAfterWait = fileSize(outPath);
        debugger.Continue();
    }

    const bool exited = stoppedAtFirstException && debugger.WaitFor(EventType::ExitProcess);
    const long sizeAfterResume = exited ? fileSize(outPath) : -1;

    const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(outFd);

    // Only now, with stdout back on the terminal, are results asserted -- same ordering
    // discipline as debugger_launch.cpp, and for the same reason.
    REQUIRE(redirectRc != -1);
    REQUIRE(restoreRc != -1);
    REQUIRE(launched);
    REQUIRE(started);
    REQUIRE(stoppedAtFirstException);

    INFO("hello_machbug wrote " << sizeAtStop << " byte(s) the instant the exception loop "
         "reported the first stop -- it should have executed nothing past dyld's own startup "
         "trap yet.");
    REQUIRE(sizeAtStop == 0);

    REQUIRE(stillStoppedAfterWait);
    INFO("hello_machbug wrote " << sizeAfterWait << " byte(s) a full second after the stop, "
         "while still parked on the unanswered exception reply. Nonzero here means the stop was "
         "never real -- the target ran anyway.");
    REQUIRE(sizeAfterWait == 0);

    REQUIRE(exited);
    INFO("hello_machbug wrote " << sizeAfterResume << " byte(s) after Continue() -- expected "
         "output ('hello machbug\\n'). Zero here means Continue() did not actually let the "
         "target run.");
    REQUIRE(sizeAfterResume > 0);
    REQUIRE(debugger.LastExitCode() == 0);

    unlink(outPath.c_str());
}

// This drives Debugger::handleException() -- the actual "park until Continue()" logic -- directly,
// with a fabricated exception rather than a real Mach message. Real Mach exception delivery does
// work end-to-end (see the two tests above, and the task report for how getting there took three
// separate bug fixes); this test is not a workaround for anything currently broken. It stays
// because it is the one piece of logic that never has to touch the OS's exception-delivery path
// at all to be exercised: that handleException() genuinely does not return -- and therefore the
// reply that would resume a real thread is genuinely withheld -- until Continue()/StepInto()/
// Stop() posts a decision. That is the single least obvious property in this file (see the class
// comment in Debugger.h), so it is worth a direct, fast, OS-independent regression test in
// addition to the end-to-end ones, not instead of them.
TEST_CASE("handleException blocks until Continue() posts a decision")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));

    std::atomic<bool> handlerReturned{false};
    std::thread handlerThread([&] {
        // MACH_PORT_NULL/0 args stand in for the fields a real exception message would carry;
        // handleException() never dereferences `thread` or `code` for a plain EXC_BREAKPOINT
        // classified as the first stop, so this is a faithful exercise of its control flow, not
        // a shortcut around it.
        debugger.handleException(MACH_PORT_NULL, EXC_BREAKPOINT, nullptr, 0);
        handlerReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !debugger.IsStopped(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(debugger.IsStopped());

    // Half a second parked with no command posted -- long enough that a handleException() which
    // (incorrectly) returned immediately instead of waiting would already have set
    // handlerReturned by now.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    REQUIRE_FALSE(handlerReturned.load(std::memory_order_acquire));
    REQUIRE(debugger.IsStopped());

    debugger.Continue();
    handlerThread.join();

    REQUIRE(handlerReturned.load(std::memory_order_acquire));
    REQUIRE_FALSE(debugger.IsStopped());

    debugger.Terminate();
}
