// Milestone 3 task 2: the arm64 half of the register contract. The descriptor-table case runs
// everywhere (a table is data); the three that touch a live thread are arm64-only, because
// thread_get_state has no way to report an arm64 thread state on x86-64 hardware.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/Arm64.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("the arm64 descriptor table describes the whole general register file")
{
    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Arm64::Descriptors(&count);
    REQUIRE(descs != nullptr);
    // x0-x30, sp, pc, pstate.
    REQUIRE(count == 34);

    bool sawPc = false;
    bool sawSp = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        INFO("descriptor " << i << " (" << descs[i].name << ")");
        REQUIRE(descs[i].name != nullptr);
        REQUIRE(descs[i].bits == 64);
        if(std::string(descs[i].name) == "pc")
        {
            sawPc = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0);
        }
        if(std::string(descs[i].name) == "sp")
        {
            sawSp = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_StackPointer) != 0);
        }
    }
    REQUIRE(sawPc);
    REQUIRE(sawSp);
}

#if defined(__arm64__) || defined(__aarch64__)

TEST_CASE("a stopped arm64 target reports a plausible pc and sp")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Arm64::Read(debugger.ResolveThread(0), &regs, &error));
    INFO("diagnostic: " << error);
    REQUIRE(regs.arch == DbgArch_Arm64);

    // Not an equality check against a hardcoded address -- ASLR makes that meaningless. What is
    // assertable is that these are addresses at all: a zeroed struct (the failure mode of a
    // Read() that reports success without filling anything) fails both.
    REQUIRE(regs.arm64.pc > 0x1000);
    REQUIRE(regs.arm64.sp > 0x1000);
    REQUIRE(regs.arm64.pc != regs.arm64.sp);
}

TEST_CASE("writing an arm64 register changes what the next read reports")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;

    // x9 rather than pc or sp: a scratch register nothing in the target's next instruction
    // depends on, so the target survives being resumed at the end of this test.
    REQUIRE(MachBug::arch::Arm64::Write(thread, "x9", 0xFEEDFACEull, &error));
    INFO("diagnostic: " << error);

    DbgRegisters regs{};
    REQUIRE(MachBug::arch::Arm64::Read(thread, &regs, &error));
    REQUIRE(regs.arm64.x[9] == 0xFEEDFACEull);
}

TEST_CASE("an unknown register name is refused with a diagnostic")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    std::string error;
    REQUIRE_FALSE(MachBug::arch::Arm64::Write(debugger.ResolveThread(0), "eax", 1, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("eax") != std::string::npos);
}

#endif
