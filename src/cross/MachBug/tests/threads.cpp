// Milestone 4 task 4: the engine noticing that a target has threads at all. Hardware breakpoints
// are per-thread on this platform, so a breakpoint armed before a thread existed has to be
// applied to that thread when it appears -- and none of that is possible until the engine can
// see the thread appear.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("a single-threaded target reports one thread and no thread events")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    REQUIRE(debugger.ThreadCount() == 1);

    // The thread that raised the first stop was there before this engine was looking. Reporting
    // it as "created" would be a fiction: the callback means a thread appeared, and a caller
    // building a thread list from these events would show a creation that never happened.
    REQUIRE(debugger.count(EventType::ThreadCreate) == 0);
    REQUIRE(debugger.count(EventType::ThreadExit) == 0);
}

TEST_CASE("the engine notices the threads a target creates")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("multi_threaded").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // At the first stop the target has executed none of its own code, so its extra threads do not
    // exist yet.
    REQUIRE(debugger.ThreadCount() == 1);

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));

    // The fixture spawns two threads that then sleep for thirty seconds, so this is a wait for
    // them to exist rather than a race against them finishing.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    debugger.Pause();
    REQUIRE(debugger.IsStopped());

    INFO("threads: " << debugger.ThreadCount() << ", ThreadCreate events: "
         << debugger.count(EventType::ThreadCreate));
    REQUIRE(debugger.ThreadCount() >= 3);
    REQUIRE(debugger.count(EventType::ThreadCreate) >= 2);

    // Every reported thread is one the engine can actually reach: an id that does not resolve
    // would be worse than no report at all, since task 5 arms debug state on exactly these.
    for(const MachBug::test::Event& event : debugger.events())
    {
        if(event.type != EventType::ThreadCreate)
            continue;
        INFO("reported thread id " << event.threadId);
        REQUIRE(debugger.ResolveThread(event.threadId) != MACH_PORT_NULL);
    }
}
