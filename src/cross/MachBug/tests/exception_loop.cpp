#include <catch2/catch_test_macros.hpp>
#include <MachBug/core/Debugger.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
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
            //
            // Bounded, not a bare join(): a test that already called WaitForLoopToFinish() will
            // find mLoopFinished already true and this is instant, but a test that did not (or a
            // genuine regression in Stop()'s own teardown -- exactly what this file exists to
            // catch) must not turn one failing test into a hung test binary. Detaching an
            // abandoned thread here leaks it, harmlessly, for the rest of the process's life
            // rather than block it forever.
            Stop();
            if(mLoopThread.joinable())
            {
                if(!mLoopFinished.load(std::memory_order_acquire))
                    WaitForLoopToFinish();
                if(mLoopFinished.load(std::memory_order_acquire))
                    mLoopThread.join();
                else
                    mLoopThread.detach();
            }
        }

        bool StartOnThread()
        {
            mLoopThread = std::thread([this] {
                Start();
                mLoopFinished.store(true, std::memory_order_release);
            });
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

        // True once Start() (running on mLoopThread, via StartOnThread()) has actually returned.
        // Polled rather than a timed std::thread::join() (which does not exist) -- see the
        // destructor for why a caller that does not want to risk hanging the whole test binary on
        // a regression needs this to be bounded rather than a bare join().
        bool WaitForLoopToFinish(const std::chrono::milliseconds timeout = std::chrono::seconds(5))
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while(!mLoopFinished.load(std::memory_order_acquire) &&
                  std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return mLoopFinished.load(std::memory_order_acquire);
        }

        int LastExitCode() const { return mExitCode; }

        // Legibility, not assertion: whichever way the two tests below land, the CI log should
        // say why without anyone having to re-derive it. cbCreateProcessEvent() fires (see
        // Debugger.Loop.cpp's Start()) only after task_set_exception_ports has already returned
        // KERN_SUCCESS and the child has been resumed with the port installed -- so if that never
        // fires, the failure is a setup problem (a bad entitlement, a denied task_for_pid, ...).
        // If it DOES fire and the test still never sees SystemBreakpoint, the port was accepted
        // by the kernel and the child ran, but no exception message ever reached mach_msg -- which
        // is exactly the wall this environment hits (see the task report). PortsInstalled() below
        // lets each test print which of those two shapes its own failure has.
        bool PortsInstalled() const { return mPortsInstalled.load(std::memory_order_acquire); }

    protected:
        void cbCreateProcessEvent(pid_t) override
        {
            mPortsInstalled.store(true, std::memory_order_release);
        }

        void cbSystemBreakpoint() override { pushEvent(EventType::SystemBreakpoint); }

        void cbExitProcessEvent(int exitCode) override
        {
            mExitCode = exitCode;
            pushEvent(EventType::ExitProcess);
        }

        void cbInternalError(const std::string & error) override
        {
            // Never silently swallowed (the base class's default is a no-op) -- stderr is not
            // redirected by either test below, so this always reaches the CI log even during the
            // "the target does not run while stopped" test's stdout capture window.
            std::fprintf(stderr, "[exception_loop] internal error: %s\n", error.c_str());
        }

    private:
        void pushEvent(const EventType type)
        {
            std::lock_guard<std::mutex> lock(mEventMutex);
            mEvents.push(type);
            mEventCv.notify_all();
        }

        std::thread mLoopThread;
        std::atomic<bool> mLoopFinished{false};
        std::mutex mEventMutex;
        std::condition_variable mEventCv;
        std::queue<EventType> mEvents;
        std::atomic<bool> mPortsInstalled{false};
        int mExitCode = -1;
    };

    void reportNoFirstStop(const LocalRecordingDebugger & debugger)
    {
        std::fprintf(stderr,
            "[exception_loop] no SystemBreakpoint event arrived within the timeout. Exception "
            "port installed before resume: %s. %s\n",
            debugger.PortsInstalled() ? "yes" : "no",
            debugger.PortsInstalled()
                ? "The kernel accepted task_set_exception_ports and the child was resumed with "
                  "the port live, but no exception message ever reached mach_msg -- this is the "
                  "sandbox exception-delivery wall documented in the task report, not a code "
                  "defect, if it also reproduces here on a GitHub Actions runner."
                : "The port was never confirmed installed before this timeout -- that points at "
                  "a setup failure (signing/entitlement/task_for_pid), a different problem than "
                  "the exception-delivery question this CI run exists to answer.");
    }
}

// This is the test the task brief asks for verbatim (Step 2), adapted to LocalRecordingDebugger
// -- see its comment for why.
TEST_CASE("the first exception stops the target until it is resumed")
{
    LocalRecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("hello_machbug").c_str()));
    REQUIRE(debugger.StartOnThread());

    const bool sawFirstStop = debugger.WaitFor(EventType::SystemBreakpoint);
    if(!sawFirstStop)
        reportNoFirstStop(debugger);
    REQUIRE(sawFirstStop);
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
    if(!stoppedAtFirstException)
        reportNoFirstStop(debugger);
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

// Drives the signal-passthrough classification directly, with a fabricated second exception,
// rather than relying on the ~1-in-10 natural occurrence that is how this bug was first found
// (see the task report). PT_ATTACHEXC (Debugger.Loop.cpp::Start()) routes every signal the child
// receives through the exception port as EXC_SOFTWARE/EXC_SOFT_SIGNAL, and the very first one --
// the SIGCONT that resumes the target -- is deliberately treated as the first stop. Any *later*
// one must be answered immediately instead of parked on, since nothing gives a caller a way to
// have asked to intercept it; parking on it left the reporting thread waiting for a Continue()
// call nobody knew to make. This test forces exactly that second case: a first call establishes
// "the first stop has already happened" (via a plain EXC_BREAKPOINT, parked and released with
// Continue(), the same as the test above), then a second call with a fabricated EXC_SOFTWARE/
// EXC_SOFT_SIGNAL exception must return promptly, without ever parking.
TEST_CASE("a second signal-passthrough exception is answered without parking")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));

    std::thread firstThread([&] {
        debugger.handleException(MACH_PORT_NULL, EXC_BREAKPOINT, nullptr, 0);
    });
    for(int i = 0; i < 500 && !debugger.IsStopped(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(debugger.IsStopped());
    debugger.Continue();
    firstThread.join();
    REQUIRE_FALSE(debugger.IsStopped());

    // EXC_SOFTWARE (5) with code[0] == EXC_SOFT_SIGNAL (0x10003) and code[1] the signal number --
    // the exact shape PT_ATTACHEXC produces for a passed-through SIGCONT (19).
    mach_exception_data_type_t code[2] = {0x10003, 19};
    std::atomic<bool> secondReturned{false};
    std::thread secondThread([&] {
        debugger.handleException(MACH_PORT_NULL, EXC_SOFTWARE, code, 2);
        secondReturned.store(true, std::memory_order_release);
    });

    // A generous window for a call that, if this were still parking (the bug), would not return
    // within it at all -- the only way to fail this loop is to time it out, which the REQUIRE
    // below then catches. Detached, not joined: if this ever regresses, secondThread is parked
    // inside handleException() forever (nothing will ever call Continue() for it -- that is
    // exactly the bug), and joining it would hang this whole test binary instead of failing one
    // assertion. Detaching means a regression here is a clean, fast FAILED, not a CI timeout.
    for(int i = 0; i < 500 && !secondReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    secondThread.detach();

    REQUIRE(secondReturned.load(std::memory_order_acquire));
    REQUIRE_FALSE(debugger.IsStopped());

    debugger.Terminate();
}

// Pins the signal-passthrough boundary from the other side: only SIGCONT is absorbed. A
// signal-passthrough exception carrying any other signal -- SIGABRT here, standing in for a
// target's own abort() or a user's SIGINT -- must still reach the ordinary cbException() path
// (parked, waiting for a decision), not be silently swallowed the way an earlier, broader version
// of this check (matching on code[0] alone, regardless of code[1]) would have. Safe to join
// unconditionally: unlike the SIGCONT test above, the *correct* behavior here always parks and
// always gets released by Continue(), so there is no regression shape that hangs this thread
// instead of just failing the REQUIRE below.
TEST_CASE("a signal-passthrough exception for a signal other than SIGCONT is not swallowed")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));

    std::thread firstThread([&] {
        debugger.handleException(MACH_PORT_NULL, EXC_BREAKPOINT, nullptr, 0);
    });
    for(int i = 0; i < 500 && !debugger.IsStopped(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(debugger.IsStopped());
    debugger.Continue();
    firstThread.join();
    REQUIRE_FALSE(debugger.IsStopped());

    // Same EXC_SOFTWARE/EXC_SOFT_SIGNAL shape as the SIGCONT case, but code[1] == SIGABRT instead.
    mach_exception_data_type_t code[2] = {0x10003, SIGABRT};
    std::atomic<bool> handlerReturned{false};
    std::thread secondThread([&] {
        debugger.handleException(MACH_PORT_NULL, EXC_SOFTWARE, code, 2);
        handlerReturned.store(true, std::memory_order_release);
    });

    // If this were (wrongly) swallowed as a passthrough, handlerReturned would already be true by
    // now instead of the call having parked.
    for(int i = 0; i < 200 && !debugger.IsStopped(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(debugger.IsStopped());
    REQUIRE_FALSE(handlerReturned.load(std::memory_order_acquire));

    debugger.Continue();
    secondThread.join();
    REQUIRE(handlerReturned.load(std::memory_order_acquire));
    REQUIRE_FALSE(debugger.IsStopped());

    debugger.Terminate();
}

// The gap that let the Stop()-deadlock Critical through: every other Stop() in this file happens
// after the target has already exited on its own (hello_machbug prints and exits; the direct-
// handler tests never Start() a loop against a live target at all), so the teardown path against
// a genuinely still-running target -- the one that actually kills it -- was never exercised. This
// test starts run_endlessly (which never exits on its own), answers its first stop so it is truly
// running freely (not merely resumed-but-still-inside-the-reply-plumbing) when Stop() is called,
// and confirms both that the loop thread actually returns (bounded -- see
// LocalRecordingDebugger::WaitForLoopToFinish()'s comment) and that the process is actually dead,
// checked independently of this class's own bookkeeping via kill(pid, 0).
TEST_CASE("Stop() kills a genuinely running target instead of hanging on its own SIGKILL")
{
    LocalRecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    REQUIRE(debugger.StartOnThread());
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const pid_t pid = debugger.GetPid();
    debugger.Continue();

    // No event to wait on for "now running freely, for real" -- give it a moment to actually
    // resume and start spinning in run_endlessly's for(;;) sleep(1) loop, so Stop() below is
    // exercised against a live, running target and not one still mid-resume.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    debugger.Stop();

    // The bug this guards against: exceptionLoop()'s teardown used to send a bare SIGKILL to a
    // target that is still ptrace(PT_ATTACHEXC)'d, with nobody left servicing its exception port
    // -- the SIGKILL itself was routed through that port as another EXC_SOFT_SIGNAL and never
    // answered, so the target never died and this call hung forever inside
    // Process::WaitForRealExit(). Bounded so a regression here is a clean, fast FAILED instead of
    // a hung test binary.
    REQUIRE(debugger.WaitForLoopToFinish());

    // The strongest confirmation available that the process is actually gone, not merely that
    // this class's own bookkeeping believes so: ask the kernel directly. kill(pid, 0) sends no
    // signal but still fails with ESRCH once pid no longer names a live process.
    errno = 0;
    const int rc = kill(pid, 0);
    INFO("kill(pid, 0) returned " << rc << ", errno " << errno << " (" << strerror(errno) << ") "
         "-- expected -1/ESRCH for a pid that is actually dead");
    REQUIRE(rc == -1);
    REQUIRE(errno == ESRCH);
}
