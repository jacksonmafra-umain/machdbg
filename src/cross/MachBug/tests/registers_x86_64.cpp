// Milestone 3 task 3: the x86-64 half of the register contract, and the dispatch that keeps
// architecture choice out of every caller. The three live-thread cases compile only on x86-64 --
// this file's assertions there are proven by the Intel CI job, not by the owner's hardware.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <MachBug/arch/Arch.h>
#include <MachBug/arch/X86_64.h>
#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("the x86-64 descriptor table publishes only fields the thread state can fill")
{
    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::X86_64::Descriptors(&count);
    REQUIRE(descs != nullptr);
    // rax rbx rcx rdx rbp rsp rsi rdi r8-r15 rip rflags cs fs gs.
    REQUIRE(count == 21);

    bool sawRip = false;
    bool sawRsp = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        INFO("descriptor " << i << " (" << descs[i].name << ")");
        REQUIRE(descs[i].name != nullptr);
        const std::string name(descs[i].name);
        // ds, es and ss are absent on purpose: x86_thread_state64_t has no field for them, and a
        // row that always reads zero is worse than a row a view never sees.
        REQUIRE(name != "ds");
        REQUIRE(name != "es");
        REQUIRE(name != "ss");
        if(name == "rip")
        {
            sawRip = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0);
        }
        if(name == "rsp")
        {
            sawRsp = true;
            REQUIRE((descs[i].flags & DbgRegisterFlag_StackPointer) != 0);
        }
    }
    REQUIRE(sawRip);
    REQUIRE(sawRsp);
}

TEST_CASE("the dispatch publishes a descriptor table for both architectures")
{
    uint32_t arm = 0;
    uint32_t intel = 0;
    REQUIRE(MachBug::arch::Descriptors(DbgArch_Arm64, &arm) != nullptr);
    REQUIRE(MachBug::arch::Descriptors(DbgArch_X86_64, &intel) != nullptr);
    REQUIRE(arm == 34);
    REQUIRE(intel == 21);

    // A caller asking for an architecture this engine does not implement gets nothing and a zero
    // count, not the host's table by accident -- which would make a wrong register file look
    // like a working one.
    uint32_t unknown = 7;
    REQUIRE(MachBug::arch::Descriptors(DbgArch_I386, &unknown) == nullptr);
    REQUIRE(unknown == 0);
}

TEST_CASE("the dispatch refuses the architecture this build cannot reach")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    DbgRegisters regs{};
    std::string error;

#if defined(__arm64__) || defined(__aarch64__)
    REQUIRE(MachBug::arch::Read(DbgArch_Arm64, thread, &regs, &error));
    REQUIRE_FALSE(MachBug::arch::Read(DbgArch_X86_64, thread, &regs, &error));
#else
    REQUIRE(MachBug::arch::Read(DbgArch_X86_64, thread, &regs, &error));
    REQUIRE_FALSE(MachBug::arch::Read(DbgArch_Arm64, thread, &regs, &error));
#endif
    // The diagnostic says the build is the reason, so nobody reads this as an unimplemented
    // feature and goes looking for the missing code.
    INFO("diagnostic: " << error);
    REQUIRE(error.find("build") != std::string::npos);

    // And an architecture nothing implements is refused too, rather than falling through to the
    // host's.
    REQUIRE_FALSE(MachBug::arch::Read(DbgArch_I386, thread, &regs, &error));
    REQUIRE_FALSE(error.empty());
}

#if defined(__x86_64__)

TEST_CASE("a stopped x86-64 target reports a plausible rip and rsp")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::X86_64::Read(debugger.ResolveThread(0), &regs, &error));
    INFO("diagnostic: " << error);
    REQUIRE(regs.arch == DbgArch_X86_64);
    REQUIRE(regs.x86_64.rip > 0x1000);
    REQUIRE(regs.x86_64.rsp > 0x1000);
    REQUIRE(regs.x86_64.rip != regs.x86_64.rsp);
}

TEST_CASE("writing an x86-64 register changes what the next read reports")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t thread = debugger.ResolveThread(0);
    std::string error;
    REQUIRE(MachBug::arch::X86_64::Write(thread, "rcx", 0xFEEDFACEull, &error));
    INFO("diagnostic: " << error);

    DbgRegisters regs{};
    REQUIRE(MachBug::arch::X86_64::Read(thread, &regs, &error));
    REQUIRE(regs.x86_64.rcx == 0xFEEDFACEull);
}

TEST_CASE("an unknown x86-64 register name is refused with a diagnostic")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    std::string error;
    REQUIRE_FALSE(MachBug::arch::X86_64::Write(debugger.ResolveThread(0), "x0", 1, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("x0") != std::string::npos);
}

#endif
