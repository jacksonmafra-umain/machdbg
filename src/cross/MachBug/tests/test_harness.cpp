// Tests the harness itself (TestHarness.h), not the engine it wraps. Task 4's tests, and every
// engine test from here to milestone 9, trust that RecordingDebugger's timeline is complete and
// ordered; this file is the one place that assumption is exercised directly rather than assumed.
#include <catch2/catch_test_macros.hpp>

#include "TestHarness.h"

#include <algorithm>
#include <string>

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::Event;
using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

// exit_code_42 (targets/exit_code_42.cpp: `int main() { return 42; }`) is the simplest possible
// fixture: no output, no faults, just an exit code to check. Like every other fixture, its first
// instruction still raises dyld's own debugger-notification trap (see Debugger.h's
// cbSystemBreakpoint comment) before it can run to exit, so this drives Continue() once, same as
// exception_loop.cpp's tests, rather than expecting ExitProcess without it.
TEST_CASE("the harness records a complete, ordered timeline for a run to completion")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("exit_code_42").c_str()));
    debugger.StartOnThread();

    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));
    REQUIRE(debugger.IsStopped());

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::ExitProcess));
    REQUIRE(debugger.LastExitCode() == 42);

    REQUIRE(debugger.WaitForLoopToFinish());

    const std::vector<Event> timeline = debugger.events();

    const auto createIt = std::find_if(timeline.begin(), timeline.end(),
        [](const Event & e) { return e.type == EventType::CreateProcess; });
    const auto exitIt = std::find_if(timeline.begin(), timeline.end(),
        [](const Event & e) { return e.type == EventType::ExitProcess; });

    INFO("timeline has " << timeline.size() << " event(s)");
    REQUIRE(createIt != timeline.end());
    REQUIRE(exitIt != timeline.end());

    // CreateProcess before ExitProcess, in that order -- both as vector position (publish order)
    // and as wall-clock timestamp, so a harness bug that recorded events out of the order they
    // actually happened in (e.g. a race in push()) would fail this even if it somehow preserved
    // vector order.
    REQUIRE(createIt < exitIt);
    REQUIRE(createIt->when <= exitIt->when);

    REQUIRE(exitIt->exitCode == 42);
    REQUIRE(debugger.count(EventType::CreateProcess) == 1);
    REQUIRE(debugger.count(EventType::ExitProcess) == 1);
}

// WaitFor()'s timeout path is a first-class part of the harness's contract (see TestHarness.h's
// comment on why it returns false rather than hanging): a Debugger that never publishes the
// awaited event must make WaitFor() return false, promptly, rather than block for the caller's
// whole test run. A plain MachBug::Debugger (not RecordingDebugger, and never Init()'d/Start()'d)
// publishes nothing at all, so waiting on it for any EventType always exercises this path.
TEST_CASE("WaitFor returns false, promptly, when the awaited event never arrives")
{
    RecordingDebugger debugger;
    const auto start = std::chrono::steady_clock::now();
    const bool result = debugger.WaitFor(EventType::CreateProcess, std::chrono::milliseconds(200));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(result);
    // Generous upper bound: this only guards against WaitFor() ignoring its timeout and hanging
    // (e.g. on the ~5s default elsewhere in this file), not against ordinary scheduling jitter.
    REQUIRE(elapsed < std::chrono::seconds(5));
}
