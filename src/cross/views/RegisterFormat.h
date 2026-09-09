#pragma once

#include <cstdint>
#include <string>

#include <MachBug/api/machbug_api.h>

// The two pieces of register presentation that need no Qt, kept here so the engine's own test
// binary can cover them without acquiring a Qt dependency -- and so the rules they encode (how
// wide a value is printed, which bytes a descriptor addresses) are asserted in one place rather
// than re-derived by every consumer.
namespace machdbg::views
{
    // Hex, zero-padded to the register's own width, lower case, no 0x prefix -- the shape
    // x64dbg's register view uses. The width matters: printing a 16-bit segment register as
    // sixteen digits claims a 64-bit register that does not exist.
    std::string FormatValue(uint64_t value, uint16_t bits);

    // Reads one register out of a DbgRegisters through a descriptor, which is the only way a
    // consumer that has just the table can do it. The descriptor's offset is relative to the
    // active arm of the union (machbug_api.h), so the arm is chosen by regs.arch here rather
    // than by the caller. Returns 0 for an architecture this build has no arm for.
    uint64_t ValueThroughDescriptor(const DbgRegisters& regs, const DbgRegisterDesc& desc);
}
