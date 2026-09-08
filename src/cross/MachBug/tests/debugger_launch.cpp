#include <catch2/catch_test_macros.hpp>
#include <MachBug/core/Debugger.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

TEST_CASE("a suspended launch yields a valid task port")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    REQUIRE(debugger.GetPid() > 0);
    REQUIRE(debugger.GetTaskPort() != MACH_PORT_NULL);
    // Start() is not running here, so there is no loop to Stop(); kill the suspended child.
    debugger.Terminate();
}

TEST_CASE("launching a path that does not exist fails cleanly")
{
    MachBug::Debugger debugger;
    REQUIRE_FALSE(debugger.Init("/nonexistent/machdbg-no-such-binary"));
    REQUIRE(debugger.GetPid() == 0);
}

namespace
{
    long fileSize(const std::string & path)
    {
        struct stat st{};
        if(stat(path.c_str(), &st) != 0)
            return -1;
        return static_cast<long>(st.st_size);
    }

    // A subclass that records the last diagnostic Init() reported, so the denial regression
    // test can assert on what the message says, not just that Init() returned false.
    struct RecordingDebugger : MachBug::Debugger
    {
        std::string lastError;

    protected:
        void cbInternalError(const std::string & error) override
        {
            lastError = error;
        }
    };
}

// This is the regression test for the property Task 4 depends on: that Init() leaves the child
// suspended rather than merely returning quickly. If POSIX_SPAWN_START_SUSPENDED were ever
// dropped, hello_machbug would print immediately and this test would fail with a message that
// says so directly, rather than as a bare "REQUIRE failed" that sends the next person looking
// in Task 4's exception loop -- the wrong place, since by then that loop exists to blame.
TEST_CASE("a suspended launch produces no output until resumed")
{
    char outPathTemplate[] = "/tmp/machbug_suspend_test.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;

    // hello_machbug inherits fd 1 at spawn time, so this process's own stdout is redirected to
    // the capture file just for the Init() call, then restored so the rest of the test binary's
    // own output is unaffected.
    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);

    MachBug::Debugger debugger;
    // Flush first: stdio buffers by FILE*, not by fd, so anything Catch2 (or this test) already
    // queued for the current stdout must be written out through the *old* fd before dup2()
    // retargets it -- otherwise a later flush could land that unrelated buffered text in the
    // capture file instead, corrupting the very measurement this test exists to take.
    std::fflush(stdout);

    // Deliberately not `REQUIRE(dup2(...) != -1)` here: Catch2's -s flag traces every passing
    // assertion to stdout as it happens, and if that trace macro's own print landed *after* the
    // redirect its side effect just performed, the trace itself would be captured into the file
    // and corrupt the very measurement below. Redirecting is therefore a bare statement; only a
    // failure path is asserted, and only once stdout is already restored (further down), so nothing
    // Catch2 might print during this call ever lands anywhere but the terminal.
    const int redirectRc = dup2(outFd, STDOUT_FILENO);

    const bool launched = redirectRc != -1 && debugger.Init(FIXTURE("hello_machbug").c_str());

    const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(outFd);

    // Both dup2 results are checked only now, with stdout already back on the terminal, so a
    // success trace under -s lands where it belongs instead of inside the capture file.
    REQUIRE(redirectRc != -1);
    REQUIRE(restoreRc != -1);
    REQUIRE(launched);

    INFO("hello_machbug wrote " << fileSize(outPath) << " byte(s) immediately after Init() "
         "returned -- it should have executed nothing yet. If this is nonzero, "
         "POSIX_SPAWN_START_SUSPENDED has regressed: the child ran before being suspended.");
    REQUIRE(fileSize(outPath) == 0);

    // A second is affordable for one test run; it is also long enough that a merely slow start
    // (rather than a truly suspended one) would have printed something by now.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    INFO("hello_machbug wrote " << fileSize(outPath) << " byte(s) a full second after Init() "
         "returned, while still meant to be suspended. Nonzero here means the child was never "
         "actually stopped -- Init() only looked suspended because nothing had scheduled it yet.");
    REQUIRE(fileSize(outPath) == 0);

    // There is no Continue() at this stage of the milestone -- resume by hand, the same way
    // Task 4's exception loop will eventually resume the child after installing exception ports.
    REQUIRE(kill(debugger.GetPid(), SIGCONT) == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    INFO("hello_machbug wrote " << fileSize(outPath) << " byte(s) after SIGCONT -- expected "
         "output ('hello machbug\\n'). Zero here means resuming the child did not let it run, "
         "which is a different bug from a suspension that never took effect.");
    REQUIRE(fileSize(outPath) > 0);

    debugger.Terminate();
    unlink(outPath.c_str());
}

// The regression test for Process::DescribeKernReturn's denial path. no_get_task_allow is
// signed ad-hoc (required to run at all on Apple Silicon) but deliberately without
// com.apple.security.get-task-allow -- see targets/no_get_task_allow.cpp and the
// machbug_fixture_no_task_allow template in cmake.toml. This reproduces the KERN_FAILURE case
// measured in docs/specs/2026-09-07-macos-port-design.md, section 9. Asserting on the message
// itself, not just the boolean, is the point: the whole purpose of DescribeKernReturn is what
// it tells the user, and a generic "task_for_pid failed" would pass a test that only checked
// REQUIRE_FALSE.
TEST_CASE("a denied task_for_pid produces a diagnostic naming the denial")
{
    RecordingDebugger debugger;
    REQUIRE_FALSE(debugger.Init(FIXTURE("no_get_task_allow").c_str()));
    REQUIRE(debugger.GetPid() == 0);

    INFO("diagnostic was: " << debugger.lastError);
    REQUIRE(debugger.lastError.find("task_for_pid") != std::string::npos);
    REQUIRE(debugger.lastError.find("KERN_FAILURE") != std::string::npos);
    REQUIRE(debugger.lastError.find("get-task-allow") != std::string::npos);
}
