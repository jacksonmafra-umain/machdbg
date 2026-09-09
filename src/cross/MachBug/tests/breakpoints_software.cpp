// Milestone 4 task 2: the bytes half of a software breakpoint -- writing the trap, remembering
// what it replaced, and putting that back exactly. What happens when the target *hits* it is
// task 3's dispatch; these tests deliberately assert only what the table itself owns.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

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

    uint64_t programCounterOf(const DbgRegisters& regs)
    {
#if defined(__arm64__) || defined(__aarch64__)
        return regs.arm64.pc;
#else
        return regs.x86_64.rip;
#endif
    }

    // Launches known_function, reads the address it prints, and leaves the target stopped with
    // that address in hand. The capture technique is memory.cpp's: the fixture inherits fd 1 at
    // spawn time, so this process's stdout is redirected for Init() and restored immediately,
    // and nothing is asserted while it is redirected -- Catch2's -s trace would otherwise land in
    // the capture file and corrupt the very line being read.
    uint64_t launchAndFindFunction(RecordingDebugger& debugger)
    {
        char outPathTemplate[] = "/tmp/machbug_known_function.XXXXXX";
        const int outFd = mkstemp(outPathTemplate);
        if(outFd == -1)
            return 0;
        const std::string outPath = outPathTemplate;

        const int savedStdout = dup(STDOUT_FILENO);
        if(savedStdout == -1)
            return 0;

        std::fflush(stdout);
        const int redirectRc = dup2(outFd, STDOUT_FILENO);
        const bool launched = redirectRc != -1 && debugger.Init(FIXTURE("known_function").c_str());
        const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
        close(savedStdout);

        if(!launched || restoreRc == -1)
        {
            close(outFd);
            unlink(outPath.c_str());
            return 0;
        }

        debugger.StartOnThread();
        if(!debugger.WaitFor(EventType::SystemBreakpoint))
        {
            close(outFd);
            unlink(outPath.c_str());
            return 0;
        }

        // Resumed so the fixture reaches its printf, then paused again: the table writes into a
        // stopped target, which is the only state a debugger patches memory in anyway.
        debugger.Continue();
        debugger.WaitFor(EventType::Resumed);

        uint64_t address = 0;
        for(int attempt = 0; attempt < 500 && address == 0; ++attempt)
        {
            if(FILE* captured = std::fopen(outPath.c_str(), "r"))
            {
                unsigned long long printed = 0;
                if(std::fscanf(captured, "%llx", &printed) == 1)
                    address = printed;
                std::fclose(captured);
            }
            if(address == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        close(outFd);
        unlink(outPath.c_str());

        debugger.Pause();
        return address;
    }
}

TEST_CASE("a software breakpoint replaces the target's bytes and puts them back")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress = launchAndFindFunction(debugger);
    INFO("the fixture published its function at 0x" << std::hex << functionAddress);
    REQUIRE(functionAddress != 0);
    REQUIRE(debugger.IsStopped());

    const mach_port_t task = debugger.GetTaskPort();
    const MachBug::arch::Trap trap = MachBug::arch::SoftwareTrap(kHostArch);
    REQUIRE(trap.size > 0);

    std::string error;
    uint8_t original[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, original, trap.size, &error));

    MachBug::Breakpoints breakpoints;
    REQUIRE(breakpoints.Add(task, kHostArch, functionAddress, DbgBreakpointKind_Software, 0,
                            &error));
    INFO("diagnostic: " << error);

    uint8_t installed[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, installed, trap.size, &error));
    REQUIRE(std::memcmp(installed, trap.bytes, trap.size) == 0);
    // The trap actually replaced something, rather than the fixture happening to start with it.
    REQUIRE(std::memcmp(installed, original, trap.size) != 0);

    REQUIRE(breakpoints.Remove(task, kHostArch, functionAddress, &error));
    uint8_t restored[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, restored, trap.size, &error));
    REQUIRE(std::memcmp(restored, original, trap.size) == 0);
    REQUIRE(breakpoints.All().empty());
}

TEST_CASE("disabling a breakpoint takes the bytes out and enabling puts them back")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress = launchAndFindFunction(debugger);
    REQUIRE(functionAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    const MachBug::arch::Trap trap = MachBug::arch::SoftwareTrap(kHostArch);
    std::string error;

    uint8_t original[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, original, trap.size, &error));

    MachBug::Breakpoints breakpoints;
    REQUIRE(breakpoints.Add(task, kHostArch, functionAddress, DbgBreakpointKind_Software, 0,
                            &error));
    REQUIRE(breakpoints.SetEnabled(task, kHostArch, functionAddress, false, &error));

    uint8_t disabled[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, disabled, trap.size, &error));
    INFO("diagnostic: " << error);
    // Disabled means the target no longer carries it -- not merely that a flag says so, which a
    // caller could not tell apart from a breakpoint that still stops the target.
    REQUIRE(std::memcmp(disabled, original, trap.size) == 0);

    const auto entry = breakpoints.Find(functionAddress);
    REQUIRE(entry.has_value());
    REQUIRE_FALSE(entry->enabled);
    REQUIRE_FALSE(entry->armed);

    REQUIRE(breakpoints.SetEnabled(task, kHostArch, functionAddress, true, &error));
    uint8_t rearmed[8]{};
    REQUIRE(MachBug::memory::Read(task, functionAddress, rearmed, trap.size, &error));
    REQUIRE(std::memcmp(rearmed, trap.bytes, trap.size) == 0);

    // Cleaned up, so the target is not left carrying a trap for whatever runs next.
    REQUIRE(breakpoints.RestoreAll(task, kHostArch, &error));
}

TEST_CASE("an unaligned software breakpoint is refused on an architecture that needs alignment")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    const uint64_t unaligned = programCounterOf(regs) + 1;

    MachBug::Breakpoints breakpoints;
    const MachBug::arch::Trap trap = MachBug::arch::SoftwareTrap(kHostArch);
    const bool added = breakpoints.Add(debugger.GetTaskPort(), kHostArch, unaligned,
                                       DbgBreakpointKind_Software, 0, &error);
    INFO("alignment requirement is " << trap.alignment << ", diagnostic: " << error);

    if(trap.alignment > 1)
    {
        // arm64: a 4-byte BRK one byte into an instruction corrupts two instructions and traps at
        // neither, so it is refused rather than written.
        REQUIRE_FALSE(added);
        REQUIRE(error.find("align") != std::string::npos);
    }
    else
    {
        // x86-64: instructions are not aligned at all, so an odd address is an ordinary place for
        // a breakpoint and refusing it would be inventing a rule the architecture does not have.
        REQUIRE(added);
        REQUIRE(breakpoints.RestoreAll(debugger.GetTaskPort(), kHostArch, &error));
    }
}

TEST_CASE("the table refuses what it cannot do yet, by name")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    MachBug::Breakpoints breakpoints;
    std::string error;
    DbgRegisters regs{};
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    const uint64_t address = programCounterOf(regs);

    // Hardware breakpoints are task 5's and watchpoints are task 6's. Until then they are refused
    // with a diagnostic that says so -- not silently downgraded to a software breakpoint, which
    // would change the target's bytes behind a caller who asked precisely not to.
    REQUIRE_FALSE(breakpoints.Add(debugger.GetTaskPort(), kHostArch, address,
                                  DbgBreakpointKind_HwExec, 0, &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());

    // And a second breakpoint at an address that already has one is refused rather than
    // double-saving the original bytes -- which would restore the trap itself on removal.
    REQUIRE(breakpoints.Add(debugger.GetTaskPort(), kHostArch, address,
                            DbgBreakpointKind_Software, 0, &error));
    REQUIRE_FALSE(breakpoints.Add(debugger.GetTaskPort(), kHostArch, address,
                                  DbgBreakpointKind_Software, 0, &error));
    INFO("diagnostic: " << error);
    REQUIRE(breakpoints.RestoreAll(debugger.GetTaskPort(), kHostArch, &error));
}

// Task 3 needs to know how far the program counter has moved by the time a trap's exception
// arrives, and that is an architecture fact best measured rather than recalled. Until the
// dispatch exists, a hit surfaces as a plain exception, which is enough to read the delta off.
TEST_CASE("a software breakpoint's trap reaches the engine, and its program counter is measurable")
{
    RecordingDebugger debugger;
    const uint64_t functionAddress = launchAndFindFunction(debugger);
    REQUIRE(functionAddress != 0);

    const mach_port_t task = debugger.GetTaskPort();
    std::string error;
    MachBug::Breakpoints breakpoints;
    REQUIRE(breakpoints.Add(task, kHostArch, functionAddress, DbgBreakpointKind_Software, 0,
                            &error));

    // The fixture calls the patched function every 50ms, so the trap fires without any nudging.
    debugger.Continue();
    const bool trapped = debugger.WaitFor(EventType::Exception);

    DbgRegisters regs{};
    const bool readRegisters =
        trapped && MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error);
    const uint64_t pc = readRegisters ? programCounterOf(regs) : 0;

    // Restored before asserting: a failure here must not leave the fixture carrying a trap, and
    // the target is killed by the harness's teardown either way.
    std::string restoreError;
    breakpoints.RestoreAll(task, kHostArch, &restoreError);

    INFO("breakpoint at 0x" << std::hex << functionAddress << ", stopped with pc 0x" << pc
         << " (delta " << std::dec << (pc - functionAddress) << " bytes)");
    REQUIRE(trapped);
    REQUIRE(readRegisters);

    // The delta is what task 3's arch::PcFixupAfterTrap has to encode, so this asserts the exact
    // number rather than a range: measured as 0 on arm64, where BRK traps without retiring, and
    // expected to be 1 on x86-64, where the INT3 byte has already executed. If x86-64 disagrees,
    // this fails on the Intel runner with both addresses in the expansion -- which is the number
    // task 3 needs, delivered by the only machine that can produce it.
    const uint64_t expectedFixup = kHostArch == DbgArch_Arm64 ? 0u : 1u;
    REQUIRE(pc == functionAddress + expectedFixup);
}
