#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

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
}
