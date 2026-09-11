// Milestone 3 task 4: the target's address space. The interesting case is the write into the
// target's own code -- unwritable as it stands, and the path milestone 4's software breakpoints
// will depend on -- which is asserted here together with the protection being restored
// afterwards, because leaving a target's code page writable is a change to the target this
// engine was never asked to make.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

#include <MachBug/arch/Arch.h>
#include <MachBug/core/Debugger.h>
#include <MachBug/memory/Memory.h>

#include "TestHarness.h"

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

namespace
{
    // The architecture whose register file this host can actually read, so the code-page test can
    // ask for a program counter without an #if at the call site.
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

TEST_CASE("memory reads and writes reach a known cell in the target")
{
    char outPathTemplate[] = "/tmp/machbug_known_globals.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;

    // The fixture inherits fd 1 at spawn time, so this process's stdout is redirected to the
    // capture file just for Init() and restored immediately -- the same technique, and the same
    // ordering discipline, as debugger_launch.cpp: nothing is asserted while stdout is
    // redirected, because Catch2's -s trace would otherwise land in the capture file.
    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);

    RecordingDebugger debugger;
    std::fflush(stdout);
    const int redirectRc = dup2(outFd, STDOUT_FILENO);
    const bool launched = redirectRc != -1 && debugger.Init(FIXTURE("known_globals").c_str());
    const int restoreRc = dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);

    REQUIRE(redirectRc != -1);
    REQUIRE(restoreRc != -1);
    REQUIRE(launched);

    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // Resumed so the fixture can reach its printf, then paused again: memory is inspected while
    // the target is stopped, which is the only state a debugger reads memory in anyway.
    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));

    uint64_t cellAddress = 0;
    uint64_t cellValue = 0;
    for(int attempt = 0; attempt < 500 && cellAddress == 0; ++attempt)
    {
        // Polls the capture file for the line rather than sleeping a fixed margin: the fixture
        // prints within milliseconds of resuming, and a poll fails in five seconds with the file
        // contents in hand instead of guessing how long is long enough.
        if(FILE* captured = std::fopen(outPath.c_str(), "r"))
        {
            unsigned long long address = 0;
            unsigned long long value = 0;
            if(std::fscanf(captured, "%llx %llx", &address, &value) == 2)
            {
                cellAddress = address;
                cellValue = value;
            }
            std::fclose(captured);
        }
        if(cellAddress == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    close(outFd);
    unlink(outPath.c_str());

    debugger.Pause();
    REQUIRE(debugger.IsStopped());

    INFO("the fixture printed address 0x" << std::hex << cellAddress << ", value 0x" << cellValue);
    REQUIRE(cellAddress != 0);
    REQUIRE(cellValue == 0x0123456789ABCDEFull);

    const mach_port_t task = debugger.GetTaskPort();
    std::string error;

    uint64_t observed = 0;
    REQUIRE(MachBug::memory::Read(task, cellAddress, &observed, sizeof(observed), &error));
    INFO("diagnostic: " << error);
    REQUIRE(observed == cellValue);

    const uint64_t replacement = 0xFEEDFACECAFEBEEFull;
    REQUIRE(MachBug::memory::Write(task, cellAddress, &replacement, sizeof(replacement), &error));
    REQUIRE(MachBug::memory::Read(task, cellAddress, &observed, sizeof(observed), &error));
    REQUIRE(observed == replacement);
}

TEST_CASE("an unmapped address is refused with a diagnostic, not a crash")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    uint64_t destination = 0;
    std::string error;
    REQUIRE_FALSE(MachBug::memory::Read(debugger.GetTaskPort(), 0x10, &destination,
                                        sizeof(destination), &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());

    // The same address is not writable either, and the failure says so rather than reporting the
    // protection flip's own error -- an unmapped page has no protection to flip.
    const uint64_t value = 1;
    REQUIRE_FALSE(MachBug::memory::Write(debugger.GetTaskPort(), 0x10, &value, sizeof(value),
                                        &error));
    INFO("diagnostic: " << error);
    REQUIRE_FALSE(error.empty());
}

TEST_CASE("a write into the target's code succeeds and leaves protection as it found it")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    // pc at the first stop is inside executable, non-writable memory -- the case milestone 4's
    // software breakpoints need, and the reason Write() flips protection at all.
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    const uint64_t code = programCounterOf(regs);
    REQUIRE(code > 0x1000);

    const mach_port_t task = debugger.GetTaskPort();
    MachBug::memory::Region before{};
    REQUIRE(MachBug::memory::RegionOf(task, code, &before, &error));
    INFO("region at the program counter: protection " << before.protection);
    REQUIRE((before.protection & VM_PROT_EXECUTE) != 0);
    REQUIRE((before.protection & VM_PROT_WRITE) == 0);

    uint8_t original = 0;
    REQUIRE(MachBug::memory::Read(task, code, &original, 1, &error));

    const uint8_t patch = static_cast<uint8_t>(original ^ 0xFF);
    REQUIRE(MachBug::memory::Write(task, code, &patch, 1, &error));
    INFO("diagnostic: " << error);

    uint8_t observed = 0;
    REQUIRE(MachBug::memory::Read(task, code, &observed, 1, &error));
    REQUIRE(observed == patch);

    MachBug::memory::Region after{};
    REQUIRE(MachBug::memory::RegionOf(task, code, &after, &error));
    INFO("protection was " << before.protection << ", is now " << after.protection);
    REQUIRE(after.protection == before.protection);

    // Put the instruction back, so the target is not left corrupted for whatever runs next.
    REQUIRE(MachBug::memory::Write(task, code, &original, 1, &error));
}

TEST_CASE("region enumeration finds the region the program counter is in")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    const uint64_t code = programCounterOf(regs);

    MachBug::memory::Region regions[512]{};
    const uint32_t count = MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions, 512);
    INFO("enumerated " << count << " region(s)");
    REQUIRE(count > 0);

    bool covered = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        if(code >= regions[i].base && code < regions[i].base + regions[i].size)
            covered = true;
    }
    REQUIRE(covered);

    // Capacity is honoured rather than overrun: asking for one region returns one.
    REQUIRE(MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions, 1) == 1);
}

TEST_CASE("every region carries a readable tag, and tag zero is not called unknown")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    std::vector<MachBug::memory::Region> regions(512);
    const uint32_t found = MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions.data(),
                                                        static_cast<uint32_t>(regions.size()));
    INFO(found << " regions");
    REQUIRE(found > 0);

    bool sawStack = false;
    bool sawUntagged = false;
    for(uint32_t i = 0; i < found; ++i)
    {
        INFO("region " << i << " at 0x" << std::hex << regions[i].base
             << " tag " << std::dec << regions[i].userTag);
        REQUIRE(regions[i].tagName != nullptr);
        REQUIRE(std::strlen(regions[i].tagName) > 0);
        if(regions[i].userTag == VM_MEMORY_STACK)
            sawStack = true;
        if(regions[i].userTag == 0)
            sawUntagged = true;
    }

    // Both are measured facts about any real process: it has a stack, and its own mapped file
    // content carries no tag at all.
    REQUIRE(sawStack);
    REQUIRE(sawUntagged);
    REQUIRE(std::string(MachBug::memory::TagName(0)) == "untagged");
    REQUIRE(std::string(MachBug::memory::TagName(VM_MEMORY_STACK)) == "stack");
}

TEST_CASE("the region walk finishes, and never reports the same base twice")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // Deliberately far more room than a process needs, so filling it means the walk did not
    // finish rather than that the buffer was small. MEASURED: a real process has under a
    // hundred regions once submaps are walked correctly; the first version of this walk reset
    // the recursion depth at every address, re-entered the shared cache submap forever, and
    // produced a hundred thousand repeated regions -- which looked exactly like "a big process"
    // from the outside.
    std::vector<MachBug::memory::Region> regions(8192);
    const uint32_t found = MachBug::memory::EnumRegions(debugger.GetTaskPort(), regions.data(),
                                                        static_cast<uint32_t>(regions.size()));
    INFO(found << " regions");
    REQUIRE(found > 0);
    REQUIRE(found < regions.size());   // the walk ended on its own, not against the buffer

    std::vector<uint64_t> bases;
    for(uint32_t i = 0; i < found; ++i)
        bases.push_back(regions[i].base);
    std::sort(bases.begin(), bases.end());
    REQUIRE(std::adjacent_find(bases.begin(), bases.end()) == bases.end());
}

TEST_CASE("the region reported for an address contains it, and can be read at its base")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    // The program counter at the first stop is inside dyld, which lives in the shared cache --
    // a submap. That is the case this test exists for: mach_vm_region_recurse overwrites the
    // address it is given with the start of whatever it found, so a descent that reuses that
    // value asks about the submap's first byte instead of the caller's address. It came back
    // with a region at 0x180000000 whose protection was 0 and whose base could not be read,
    // for an address that was perfectly readable.
    DbgRegisters regs{};
    std::string error;
    REQUIRE(MachBug::arch::Read(kHostArch, debugger.ResolveThread(0), &regs, &error));
    const uint64_t pc = programCounterOf(regs);
    REQUIRE(pc > 0x1000);

    uint8_t byte = 0;
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), pc, &byte, 1, &error));

    MachBug::memory::Region region{};
    REQUIRE(MachBug::memory::RegionOf(debugger.GetTaskPort(), pc, &region, &error));
    INFO("pc 0x" << std::hex << pc << " is in 0x" << region.base << "+0x" << region.size
         << " prot " << std::dec << region.protection << " depth " << region.depth);

    REQUIRE(pc >= region.base);
    REQUIRE(pc < region.base + region.size);
    // Executable, because the program counter is in it -- a region with no protection at all is
    // a reservation rather than a mapping, and reporting one as "the region containing this
    // address" is what sent a memory panel to an address it could not read.
    REQUIRE(region.protection != 0);
    REQUIRE(MachBug::memory::Read(debugger.GetTaskPort(), region.base, &byte, 1, &error));
}
