#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>
#include <MachBug/arch/Arch.h>

// The x86-64 half of the register contract: one thread-state flavor (x86_THREAD_STATE64, flavor
// 4), one mapping into DbgRegsX86_64, one descriptor table. The arm64 twin is arch/Arm64.h; read
// its header comment for why these are free functions over a thread port rather than ElfBug's
// accessor-per-register shape.
//
// No StripPointerAuth() here, deliberately: x86-64 has no signed pointers, and an identity
// function would invite call sites to strip on both architectures as if the concept were shared.
namespace MachBug::arch::X86_64
{
    // The register file as data. Answers on every host -- a table is data, and a view running on
    // Apple Silicon still needs to be able to learn the x86-64 register file.
    const DbgRegisterDesc* Descriptors(uint32_t* count);

    // Fills `out` from `thread`, which must be a thread of a stopped target. Returns false and
    // sets `error` (when non-null) on failure, including on a build that is not x86-64 at all.
    bool Read(mach_port_t thread, DbgRegisters* out, std::string* error);

    // Writes one register by the name the descriptor table publishes. Reads the whole state,
    // replaces one field and writes it back, because thread_set_state has no narrower unit.
    bool Write(mach_port_t thread, const char* name, uint64_t value, std::string* error);

    // Arms or disarms hardware single-step for one thread. Two mechanisms, one entry (spec
    // section 6): MDSCR_EL1 bit 0 on arm64, the TF bit in rflags on x86-64. Both are per-thread,
    // like every other piece of debug state on this platform.
    bool SetSingleStep(mach_port_t thread, bool enable, std::string* error);

    // Whether an exception is the trap a single-step produces. Needed because arming a step and
    // then treating the *next* exception as its completion is wrong: a signal passthrough can
    // arrive first, and reporting that as a completed step tells a caller the target advanced
    // when it did not.
    bool IsSingleStepTrap(exception_type_t exception, const int64_t* code,
                          uint32_t codeCnt);

    // The four debug address registers DR0-DR3, which is an architectural constant rather than
    // something to ask the machine about -- unlike arm64, where the slot count varies and the
    // array width lies. Both counts report the same four because the same four registers serve
    // execution breakpoints and watchpoints here: they are one pool, and a watchpoint (task 6)
    // takes a slot an execution breakpoint could otherwise have used.
    DebugSlotCounts SlotCounts();

    // Writes the execution slots into DR0-DR3 and their DR7 enable and type fields, clearing the
    // slots `slots` does not describe. The rest of DR7 -- and the TF bit, which lives in rflags
    // rather than here -- is left as it was.
    bool ApplyDebugState(mach_port_t thread, const DebugSlots& slots, std::string* error);

    // See arch::DecodeDebugTrap for what each architecture's exception does and does not say.
    DebugTrap DecodeDebugTrap(mach_port_t thread, exception_type_t exception, const int64_t* code,
                              uint32_t codeCnt);
}
