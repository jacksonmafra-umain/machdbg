#pragma once

#include <mach/mach.h>
#include <mach/exception_types.h>
#include <cstdint>
#include <string>
#include <vector>

#include <MachBug/api/machbug_api.h>

// The one place an architecture is chosen. Above this, nothing branches on DbgArch: the C API
// (api/machbug_api.cpp) calls these three, and a register view iterates whatever table comes
// back (spec section 5).
namespace MachBug::arch
{
    // Answers for both architectures on both hosts -- a descriptor table is data, not a call
    // into the kernel. Returns nullptr and sets *count to 0 for an architecture this engine does
    // not implement, so a caller cannot be handed the wrong register file by accident.
    const DbgRegisterDesc* Descriptors(DbgArch arch, uint32_t* count);

    // Reads and writes through the flavor `arch` names. Only the host's own architecture can be
    // reached: thread_get_state has no way to report an x86-64 thread state on arm64 hardware,
    // and a target running under Rosetta exposes the translator's state rather than the emulated
    // one's -- out of scope for this milestone. The refusal carries that reason as its
    // diagnostic instead of looking like a missing feature.
    bool Read(DbgArch arch, mach_port_t thread, DbgRegisters* out, std::string* error);
    bool Write(DbgArch arch, mach_port_t thread, const char* name, uint64_t value,
               std::string* error);

    // Single-step, per thread, through whichever mechanism `arch` names. Same host restriction as
    // Read/Write: only the architecture this build was made for can be armed.
    bool SetSingleStep(DbgArch arch, mach_port_t thread, bool enable, std::string* error);

    // See the per-architecture comment: on x86-64 the single-step trap has its own subcode
    // (EXC_I386_SGL) and is distinguishable from a breakpoint fault; on arm64 both arrive as
    // EXC_ARM_BREAKPOINT, so this is necessary but not sufficient there and the breakpoint table
    // is what tells them apart.
    bool IsSingleStepTrap(DbgArch arch, exception_type_t exception, const int64_t* code,
                          uint32_t codeCnt);

    // The software trap for an architecture: the bytes to write, how many, and the alignment an
    // address must satisfy to hold them. arm64's BRK #0 is 0xD4200000 and must sit on a 4-byte
    // boundary because every arm64 instruction does; x86-64's INT3 is one byte and needs none.
    // Verified with the assembler rather than quoted from memory -- `as -arch arm64` on `brk #0`
    // emits d4200000, and `int3` emits cc.
    //
    // An architecture this engine has no trap for gets a zero size, so a caller cannot patch a
    // target with an empty or borrowed encoding.
    struct Trap
    {
        const uint8_t* bytes;
        uint32_t size;
        uint32_t alignment;
    };

    Trap SoftwareTrap(DbgArch arch);

    // How far the program counter has moved past a software trap by the time its exception
    // arrives, in bytes. Measured, not recalled: 0 on arm64, where BRK traps without retiring,
    // and 1 on x86-64, where the INT3 byte has already executed -- both asserted exactly by
    // tests/breakpoints_software.cpp on their own runners.
    uint32_t PcFixupAfterTrap(DbgArch arch);

    // How many debug slots this machine really has. NOT the width of the kernel's arrays:
    // arm_debug_state64_t declares __bvr[16]/__wvr[16] on every Apple Silicon machine, and this
    // one implements six execution breakpoints and four watchpoints. Writing slot 7 writes a
    // register the CPU does not have.
    struct DebugSlotCounts
    {
        uint32_t exec;
        uint32_t watch;
    };

    DebugSlotCounts SlotCounts(DbgArch arch);

    // What one thread's debug registers should hold: `exec[i]` is the address in execution slot
    // i, and 0 means the slot is empty. Sized by the caller to whatever it wants written; slots
    // beyond its end are cleared, so one table describes a thread's whole debug state rather
    // than a patch to it.
    //
    // Per thread, not per task -- that is the fact this whole task is shaped around. A hardware
    // breakpoint is one thread_set_state per live thread, and a thread born afterwards carries
    // none of them until it is written too.
    struct DebugSlots
    {
        std::vector<uint64_t> exec;
    };

    // Writes `slots` into one thread, leaving every debug register these slots do not describe
    // as it was -- single-step lives in the same state on arm64 (MDSCR_EL1) and in the same
    // register file on x86-64, and clobbering it here would disarm a step the engine is in the
    // middle of.
    bool ApplyDebugState(DbgArch arch, mach_port_t thread, const DebugSlots& slots,
                         std::string* error);
}
