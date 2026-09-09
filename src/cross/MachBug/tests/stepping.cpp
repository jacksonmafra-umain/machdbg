// Milestone 4 task 1: single-step, now reachable from outside the exception loop. The mechanism
// is unchanged -- MDSCR_EL1 bit 0 on arm64, the TF bit in rflags on x86-64 -- but until this task
// it lived in an anonymous namespace in core/Debugger.Loop.cpp, where the breakpoint code that
// needs it for the restore-step-re-arm cycle could not reach it.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/Arch.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

namespace
{
#if defined(__arm64__) || defined(__aarch64__)
    constexpr DbgArch kHostArch = DbgArch_Arm64;
#else
    constexpr DbgArch kHostArch = DbgArch_X86_64;
#endif

    uint64_t programCounterOf(const DbgRegisters& regs)
    {
#if defined(__arm64__) || defined(__aarch64__)
        return regs.arm64.pc;
#else
        return regs.x86_64.rip;
#endif
    }
}

TEST_CASE("a step advances the program counter and leaves the target stopped")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters before{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &before, &error));

    debugger.StepInto();
    REQUIRE(debugger.WaitFor(EventType::Step));
    REQUIRE(debugger.IsStopped());

    DbgRegisters after{};
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &after, &error));
    INFO("pc was 0x" << std::hex << programCounterOf(before) << ", is now 0x"
         << programCounterOf(after));
    REQUIRE(programCounterOf(after) != programCounterOf(before));

    // One instruction, not a resume: run_endlessly never exits, so an ExitProcess here would mean
    // the step let it run freely.
    REQUIRE(debugger.count(EventType::ExitProcess) == 0);
}

TEST_CASE("single-step is reachable through the arch dispatch")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;
    REQUIRE(MachBug::arch::SetSingleStep(kHostArch, thread, true, &error));
    INFO("diagnostic: " << error);
    REQUIRE(MachBug::arch::SetSingleStep(kHostArch, thread, false, &error));

    // A thread that does not exist is refused with a diagnostic rather than reported as armed.
    REQUIRE_FALSE(MachBug::arch::SetSingleStep(kHostArch, MACH_PORT_NULL, true, &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());

    // And so is an architecture this build cannot reach at all, for the same reason Read and
    // Write refuse it: there is no such thread state here to arm.
#if defined(__arm64__) || defined(__aarch64__)
    REQUIRE_FALSE(MachBug::arch::SetSingleStep(DbgArch_X86_64, thread, true, &error));
#else
    REQUIRE_FALSE(MachBug::arch::SetSingleStep(DbgArch_Arm64, thread, true, &error));
#endif
    REQUIRE(error.find("build") != std::string::npos);
}
