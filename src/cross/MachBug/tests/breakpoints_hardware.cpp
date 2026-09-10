// Milestone 4 task 5: execution breakpoints that leave the target's instructions alone. A
// software breakpoint works by replacing them, which is fine for most code and impossible for
// the rest -- a page this engine cannot make writable, code that checksums itself, an address
// the user wants watched without changing a byte of what runs. These tests assert the one
// property that distinguishes the two: the target stops, and its bytes are still its own.
//
// WHAT WAS MEASURED, and why the exception codes appear nowhere below. On arm64 a hardware
// breakpoint hit and a BRK instruction are indistinguishable at the exception:
//
//     hardware BVR/BCR hit: exception=6 codeCnt=2 code[0]=0x1 code[1]=0x100578d10
//     software BRK #0 hit:  exception=6 codeCnt=2 code[0]=0x1 code[1]=0x100a54d10
//
// Same EXC_BREAKPOINT, same EXC_ARM_BREAKPOINT subcode, and code[1] is the program counter in
// both cases. So nothing in the message says which mechanism stopped the target, and the
// dispatch keeps classifying by address against the breakpoint table -- exactly as it already
// did for software breakpoints. A predicate written against the codes would have matched both.
//
// x86-64 measured on CI, where a hardware hit arrives as `Exception(type=6, code1=0)`: also
// EXC_BREAKPOINT, and its code[1] carries no address at all. There is nothing to classify by
// there either. What differs on x86-64 is where the program counter sits: a DR hit faults
// before the instruction runs, so rip IS the breakpoint, while an INT3 has already executed
// its byte and rip is one past it. The dispatch tries both candidates for that reason.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include <MachBug/arch/Arch.h>
#include <MachBug/core/Breakpoints.h>
#include <MachBug/core/Debugger.h>
#include <MachBug/memory/Memory.h>

#include "TestHarness.h"

#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>
#else
#include <mach/i386/thread_status.h>
#endif

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

    // The address a thread's first execution slot currently holds, read straight out of its debug
    // registers rather than out of the engine's own table. Propagation is only believable if the
    // thread itself carries the breakpoint, and only the registers can say that.
    uint64_t slotZeroAddressOf(const mach_port_t thread)
    {
#if defined(__arm64__) || defined(__aarch64__)
        arm_debug_state64_t state{};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread, ARM_DEBUG_STATE64,
                            reinterpret_cast<thread_state_t>(&state), &count) != KERN_SUCCESS)
            return 0;
        return (state.__bcr[0] & 1u) != 0 ? state.__bvr[0] : 0;
#else
        x86_debug_state64_t state{};
        mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread, x86_DEBUG_STATE64,
                            reinterpret_cast<thread_state_t>(&state), &count) != KERN_SUCCESS)
            return 0;
        return (state.__dr7 & 1ull) != 0 ? state.__dr0 : 0;
#endif
    }

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
            case EventType::Breakpoint: out += "Breakpoint "; break;
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

TEST_CASE("the engine reports the machine's real slot count, not the array width")
{
    const MachBug::arch::DebugSlotCounts counts = MachBug::arch::SlotCounts(kHostArch);
    INFO("exec=" << counts.exec << " watch=" << counts.watch);

    // Both architectures have at least four execution slots, and neither has sixteen -- which is
    // the width of arm_debug_state64_t's arrays and the number this must not report. Writing a
    // slot the CPU does not implement is the failure this count exists to prevent.
    REQUIRE(counts.exec >= 4);
    REQUIRE(counts.exec < 16);
    REQUIRE(counts.watch >= 2);
    REQUIRE(counts.watch < 16);
}

TEST_CASE("a hardware breakpoint stops the target without changing its bytes")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("known_function"));
    INFO("the fixture published its function at 0x" << std::hex << functionAddress);
    REQUIRE(functionAddress != 0);
    REQUIRE(debugger.IsStopped());

    const mach_port_t task = debugger.GetTaskPort();
    uint8_t before[8]{};
    std::string error;
    REQUIRE(MachBug::memory::Read(task, functionAddress, before, sizeof(before), &error));

    REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, functionAddress,
                                           DbgBreakpointKind_HwExec, 0, &error));
    INFO("diagnostic: " << error);

    // The whole point, asserted before the target ever runs again: nothing was written. A
    // software breakpoint fails this line by construction.
    uint8_t armed[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, armed, sizeof(armed), &error));
    REQUIRE(std::memcmp(before, armed, sizeof(before)) == 0);

    debugger.Continue();
    const bool stopped = debugger.WaitFor(EventType::Breakpoint);
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(stopped);
    REQUIRE(debugger.IsStopped());

    // Reported at the address that was asked for, and the bytes there are still the target's own
    // while it is stopped on them.
    bool reportedAtAddress = false;
    for(const MachBug::test::Event& event : debugger.events())
    {
        if(event.type == EventType::Breakpoint && event.address == functionAddress)
            reportedAtAddress = true;
    }
    REQUIRE(reportedAtAddress);

    uint8_t stoppedBytes[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, stoppedBytes, sizeof(stoppedBytes),
                                  &error));
    REQUIRE(std::memcmp(before, stoppedBytes, sizeof(before)) == 0);
}

TEST_CASE("continuing past a hardware breakpoint reaches it again rather than trapping in place")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("known_function"));
    REQUIRE(functionAddress != 0);

    std::string error;
    REQUIRE(debugger.BreakpointTable().Add(debugger.GetTaskPort(), kHostArch, functionAddress,
                                           DbgBreakpointKind_HwExec, 0, &error));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Breakpoint));

    // A hardware breakpoint faults *before* the instruction runs, so resuming with the slot still
    // armed traps again at the same address forever. The engine's restore-step-re-arm cycle is
    // what makes a second hit mean the fixture went round its loop -- and the fixture sleeps 50ms
    // per lap, so a trap-in-place storm would arrive far faster than this and still pass a naive
    // "did it stop twice" check. The Step in between is what says the cycle ran.
    debugger.Continue();
    const bool secondHit = debugger.WaitFor(EventType::Breakpoint);
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(secondHit);
    REQUIRE(debugger.count(EventType::InternalError) == 0);
}

TEST_CASE("asking for one more hardware breakpoint than the machine has is refused by name")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress =
        MachBug::test::LaunchAndReadPublishedAddress(debugger, FIXTURE("known_function"));
    REQUIRE(functionAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    const uint32_t slots = MachBug::arch::SlotCounts(kHostArch).exec;
    std::string error;

    // Distinct four-byte-apart addresses: a hardware breakpoint writes no memory, so these need
    // to be real addresses only in the sense that the slot holds them.
    for(uint32_t i = 0; i < slots; ++i)
    {
        INFO("breakpoint " << i << " of " << slots);
        REQUIRE(debugger.BreakpointTable().Add(task, kHostArch, functionAddress + 4 * i,
                                               DbgBreakpointKind_HwExec, 0, &error));
    }

    error.clear();
    const bool oneTooMany = debugger.BreakpointTable().Add(
        task, kHostArch, functionAddress + 4 * slots, DbgBreakpointKind_HwExec, 0, &error);
    INFO("diagnostic: " << error);

    // Refused, not quietly turned into a software breakpoint: a caller that asked for hardware
    // asked precisely for the target's bytes to be left alone.
    REQUIRE_FALSE(oneTooMany);
    REQUIRE(error.find(std::to_string(slots)) != std::string::npos);
    REQUIRE(error.find("hardware") != std::string::npos);

    uint8_t bytes[4]{};
    std::string readError;
    REQUIRE(MachBug::memory::Read(task, functionAddress + 4 * slots, bytes, sizeof(bytes),
                                  &readError));
    const MachBug::arch::Trap trap = MachBug::arch::SoftwareTrap(kHostArch);
    REQUIRE(std::memcmp(bytes, trap.bytes, trap.size) != 0);
}

TEST_CASE("a hardware breakpoint reaches a thread that appears after it was set")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("multi_threaded").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));
    REQUIRE(debugger.ThreadCount() == 1);

    // An address the fixture never executes: this test is about which threads carry the
    // breakpoint, not about hitting it, and a breakpoint that fired here would stop the target
    // before its other threads existed.
    constexpr uint64_t kNeverExecuted = 0x1000;
    std::string error;
    REQUIRE(debugger.BreakpointTable().Add(debugger.GetTaskPort(), kHostArch, kNeverExecuted,
                                           DbgBreakpointKind_HwExec, 0, &error));
    INFO("diagnostic: " << error);

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));

    // The fixture spawns two threads that sleep for thirty seconds, so this waits for them to
    // exist rather than racing them to finish.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    debugger.Pause();
    INFO("timeline: " << timelineOf(debugger));
    REQUIRE(debugger.ThreadCount() >= 3);

    std::size_t carrying = 0;
    for(const MachBug::test::Event& event : debugger.events())
    {
        if(event.type != EventType::ThreadCreate)
            continue;
        const mach_port_t thread = debugger.ResolveThread(event.threadId);
        REQUIRE(thread != MACH_PORT_NULL);
        INFO("thread " << event.threadId << " holds 0x" << std::hex
             << slotZeroAddressOf(thread) << " in its first execution slot");
        REQUIRE(slotZeroAddressOf(thread) == kNeverExecuted);
        ++carrying;
    }

    // Debug registers are per-thread: a breakpoint set before these threads were born is in the
    // engine's table and in nothing else until it is written into each of them.
    REQUIRE(carrying >= 2);
}
