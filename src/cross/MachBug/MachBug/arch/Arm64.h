#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

// The arm64 half of the register contract: one thread-state flavor (ARM_THREAD_STATE64, flavor
// 6, count 68), one mapping into DbgRegsArm64, one descriptor table. Nothing above the C API
// learns any of this -- that is what the table is for (spec section 5).
//
// This file is also the single place pointer authentication is dealt with (spec section 6): a
// stripped pc must be stripped here, before disassembly, symbolisation or stack walking ever
// sees it, rather than at each call site.
//
// A deliberate divergence from ElfBug, whose shape does not transfer: ElfBug::Registers
// (ElfBug/thread/Registers.cpp) is a per-tid object with one accessor method per register
// (Rax(), Rbx(), ...), because ptrace hands over a whole user_regs_struct that a caller then
// pokes at. Here the register file is published as data instead, and these are free functions
// over a thread port -- an accessor per register would have to be written twice, once per
// architecture, and every view above would then need to know which set to call. The descriptor
// table exists precisely to make that unnecessary.
namespace MachBug::arch::Arm64
{
    // The register file as data: x0-x30, sp, pc, pstate. `count` receives the number of entries;
    // the returned pointer is to static storage and outlives every caller. Answers on every host
    // -- a table is data, and Descriptors(DbgArch_Arm64) has to work on the Intel runner too.
    const DbgRegisterDesc* Descriptors(uint32_t* count);

    // Fills `out` from `thread`, which must be a thread of a target that is stopped. Returns
    // false and sets `error` (when non-null) to a named diagnostic on failure -- an invalid
    // thread port and a thread that has gone away are both ordinary outcomes here, not
    // programming errors.
    bool Read(mach_port_t thread, DbgRegisters* out, std::string* error);

    // Writes one register by the name the descriptor table publishes. Reads the whole state,
    // replaces one field and writes it back, because thread_set_state has no narrower unit.
    bool Write(mach_port_t thread, const char* name, uint64_t value, std::string* error);

    // Arms or disarms hardware single-step for one thread. Two mechanisms, one entry (spec
    // section 6): MDSCR_EL1 bit 0 on arm64, the TF bit in rflags on x86-64. Both are per-thread,
    // like every other piece of debug state on this platform.
    bool SetSingleStep(mach_port_t thread, bool enable, std::string* error);

    // Removes an arm64e pointer-authentication signature from a code address. See the comment on
    // the implementation for what was measured about when a signature is actually present.
    uint64_t StripPointerAuth(uint64_t address);
}
