#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

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
}
