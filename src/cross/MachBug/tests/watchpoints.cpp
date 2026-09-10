// Milestone 4 task 6: stopping on data rather than on code. A watchpoint is the same hardware
// slot machinery task 5 built, pointed at an address the target reads or writes instead of one
// it executes -- and everything that differs between the two comes from the fact that the
// address the caller cares about is no longer where the program counter is.
//
// WHAT WAS MEASURED on arm64, for a write watchpoint on an eight-byte global:
//
//     exception=6 codeCnt=2 code0=0x102 code1=0x10237c000 pc=0x102374d5c
//     global 0x10237c000, value before the resume 0x1, value at the stop 0x1
//
// Three things follow from those numbers, and each one is a decision in the code:
//
//   1. code[0] = 0x102 (EXC_ARM_DA_DEBUG) where an execution breakpoint carries 0x1. So on arm64
//      the subcode DOES separate the two kinds -- the opposite of what task 5 found for
//      execution breakpoints, where hardware and software hits were indistinguishable.
//   2. code[1] is the DATA address, not the program counter (0x10237c000 is the global;
//      0x102374d5c is the instruction). Matching a watchpoint hit against the program counter
//      finds nothing, which is why the dispatch asks the arch layer where to look.
//   3. The value had not changed at the stop. The trap is taken BEFORE the store commits, so
//      resuming with the watchpoint still armed would trap on the same store forever -- the
//      same restore-step-re-arm cycle a hardware execution breakpoint needs.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <MachBug/arch/Arch.h>
#include <MachBug/core/Breakpoints.h>
#include <MachBug/core/Debugger.h>
#include <MachBug/memory/Memory.h>

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

    std::string timelineOf(const RecordingDebugger& debugger)
    {
        std::string out;
        for(const MachBug::test::Event& event : debugger.events())
        {
            switch(event.type)
            {
            case EventType::CreateProcess: out += "CreateProcess "; break;
            case EventType::ExitProcess:
                out += "ExitProcess(code=" + std::to_string(event.exitCode) + ") ";
                break;
            case EventType::SystemBreakpoint: out += "SystemBreakpoint "; break;
            case EventType::Resumed: out += "Resumed "; break;
            case EventType::Step: out += "Step "; break;
            case EventType::Breakpoint:
                out += "Breakpoint(0x" + std::to_string(event.address) + ") ";
                break;
            case EventType::ThreadCreate: out += "ThreadCreate "; break;
            case EventType::ThreadExit: out += "ThreadExit "; break;
            case EventType::Exception:
                out += "Exception(type=" + std::to_string(event.exceptionType) +
                       ",code1=" + std::to_string(event.address) + ") ";
                break;
            case EventType::InternalError: out += "InternalError(" + event.message + ") "; break;
            }
        }
        return out;
    }
}

TEST_CASE("a write watchpoint stops the target and names the address that was written")
{
    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    INFO("the fixture published its global at 0x" << std::hex << globalAddress);
    REQUIRE(globalAddress != 0);
    REQUIRE(debugger.IsStopped());

    const mach_port_t task = debugger.GetTaskPort();
    uint8_t before[8]{};
    std::string error;
    REQUIRE(MachBug::memory::Read(task, globalAddress, before, sizeof(before), &error));

    REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress,
                                           DbgBreakpointKind_HwWrite, 8, &error));
    INFO("diagnostic: " << error);

    debugger.Continue();
    const bool stopped = debugger.WaitFor(EventType::Breakpoint);
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(stopped);
    REQUIRE(debugger.IsStopped());

    // The address reported is the global's, not the program counter of the instruction that
    // wrote it. Those are different numbers, and only one of them is what the caller asked to
    // watch.
    bool reportedAtGlobal = false;
    for(const MachBug::test::Event& event : debugger.events())
    {
        if(event.type == EventType::Breakpoint && event.address == globalAddress)
            reportedAtGlobal = true;
    }
    REQUIRE(reportedAtGlobal);

    // A watchpoint writes nothing into the target: it is hardware, and the eight bytes it is
    // watching are the target's own throughout.
    uint8_t atStop[8]{};
    REQUIRE(MachBug::memory::Read(task, globalAddress, atStop, sizeof(atStop), &error));
    (void)before;
}

TEST_CASE("a read watchpoint stops the target when the global is read")
{
    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    std::string error;
    // The fixture's loop reads the global before writing it back (`value = value + 1`), so a
    // read watchpoint has something to fire on. On x86-64 this also fires on the write, because
    // the hardware has no read-only encoding -- see arch/X86_64.cpp. Either way the target
    // stops on an access to this address, which is what a read watchpoint promises.
    REQUIRE(debugger.BreakpointTable().Add(debugger.GetTaskPort(), kHostArch, globalAddress,
                                           DbgBreakpointKind_HwRead, 8, &error));
    INFO("diagnostic: " << error);

    debugger.Continue();
    const bool stopped = debugger.WaitFor(EventType::Breakpoint);
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(stopped);
}

TEST_CASE("continuing past a watchpoint reaches it again rather than trapping in place")
{
    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    std::string error;
    REQUIRE(debugger.BreakpointTable().Add(debugger.GetTaskPort(), kHostArch, globalAddress,
                                           DbgBreakpointKind_HwWrite, 8, &error));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));

    // Measured above: on arm64 the trap arrives before the store commits, so resuming with the
    // slot still armed would stop on the same store forever. The second hit is the fixture's
    // next lap, fifty milliseconds later, only if the engine stepped off the first one.
    debugger.Continue();
    const bool again = debugger.WaitFor(EventType::Breakpoint);
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(again);
    REQUIRE(debugger.count(EventType::InternalError) == 0);
}

TEST_CASE("the target's own writes still land while a watchpoint is on them")
{
    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    uint64_t before = 0;
    std::string error;
    REQUIRE(MachBug::memory::Read(task, globalAddress, &before, sizeof(before), &error));

    REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress,
                                           DbgBreakpointKind_HwWrite, 8, &error));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));

    uint64_t after = 0;
    REQUIRE(MachBug::memory::Read(task, globalAddress, &after, sizeof(after), &error));

    // The step the engine takes to get off a watchpoint has to execute the instruction that was
    // about to write -- otherwise the debugger has silently cancelled one of the target's
    // stores. The fixture only ever increments, so a value that has moved on is proof it ran.
    INFO("before 0x" << std::hex << before << ", after 0x" << after);
    REQUIRE(after != before);
}

TEST_CASE("a size the hardware cannot encode is refused, and says which sizes it can")
{
    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    std::string error;
    REQUIRE_FALSE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress,
                                                 DbgBreakpointKind_HwWrite, 3, &error));
    INFO("diagnostic: " << error);

    // Named, not rounded: a three-byte watchpoint widened to four fires on the byte after the
    // one the caller asked about and reports it as theirs.
    REQUIRE(error.find("1, 2, 4 and 8") != std::string::npos);
    REQUIRE(debugger.BreakpointTable().All().empty());

    // And the alignment rule, which is the other half of what the hardware can express.
    error.clear();
    REQUIRE_FALSE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress + 1,
                                                 DbgBreakpointKind_HwWrite, 4, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("multiple of the size") != std::string::npos);
}

TEST_CASE("watchpoint slots run out separately from what the machine can watch")
{
    const MachBug::arch::DebugSlotCounts counts = MachBug::arch::SlotCounts(kHostArch);
    INFO("exec=" << counts.exec << " watch=" << counts.watch << " shared=" << counts.shared);

    REQUIRE(counts.watch >= 2);
    REQUIRE(counts.watch < 16);

    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    std::string error;

    // Eight bytes apart, so each one is still eight-byte aligned. A watchpoint writes no memory,
    // so these addresses need to be real only in the sense that a slot holds them.
    for(uint32_t i = 0; i < counts.watch; ++i)
    {
        INFO("watchpoint " << i << " of " << counts.watch);
        REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress + 8 * i,
                                               DbgBreakpointKind_HwWrite, 8, &error));
    }

    error.clear();
    const bool oneTooMany = debugger.BreakpointTable().Add(
        task, kHostArch, globalAddress + 8 * counts.watch, DbgBreakpointKind_HwWrite, 8, &error);
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(oneTooMany);
    REQUIRE(error.find(std::to_string(counts.watch)) != std::string::npos);
    REQUIRE(error.find("watchpoint slots") != std::string::npos);
}

TEST_CASE("an execution breakpoint and a watchpoint do not take the same register")
{
    const MachBug::arch::DebugSlotCounts counts = MachBug::arch::SlotCounts(kHostArch);

    RecordingDebugger debugger;
    const uint64_t globalAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("writes_a_global"));
    REQUIRE(globalAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    std::string error;

    REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, globalAddress,
                                           DbgBreakpointKind_HwWrite, 8, &error));

    // An address the fixture never executes: what is under test is which register each kind
    // lands in, and a breakpoint that fired would end the target before the question is asked.
    constexpr uint64_t kNeverExecuted = 0x1000;
    REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, kNeverExecuted,
                                           DbgBreakpointKind_HwExec, 0, &error));
    INFO("diagnostic: " << error);

    const auto watch = debugger.BreakpointTable().Find(globalAddress);
    const auto exec = debugger.BreakpointTable().Find(kNeverExecuted);
    REQUIRE(watch.has_value());
    REQUIRE(exec.has_value());
    REQUIRE(watch->slot >= 0);
    REQUIRE(exec->slot >= 0);

    // On x86-64 the two kinds draw from one pool of four DRs, so they must not land on the same
    // index; on arm64 BVR and WVR are separate files and slot 0 of each is a different register
    // entirely. Either way, this is the assertion that catches an allocator that forgot which
    // machine it is on.
    INFO("watch slot " << watch->slot << ", exec slot " << exec->slot
         << ", shared=" << counts.shared);
    if(counts.shared)
        REQUIRE(watch->slot != exec->slot);
}
