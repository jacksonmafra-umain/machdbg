#pragma once

#include <mach/mach.h>
#include <cstdint>
#include <string>

// The target's address space. Every mach_vm_* call in the engine lives here; nothing above this
// file names one, which is what keeps kern_return_t out of the C API (spec section 5).
namespace MachBug::memory
{
    struct Region
    {
        uint64_t base = 0;
        uint64_t size = 0;
        uint32_t protection = 0;      // current, VM_PROT_* bits
        uint32_t maxProtection = 0;
        uint32_t userTag = 0;         // VM_MEMORY_*, filled by EnumRegions only
    };

    bool Read(mach_port_t task, uint64_t address, void* dest, uint64_t size, std::string* error);

    // Writes into the target, flipping protection when the page cannot be written as it stands:
    // mach_vm_protect(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY), write, restore. VM_PROT_COPY is
    // the part that matters for a code page -- it asks for a private copy rather than write
    // access to the shared, signed mapping, which is the only way a debugger gets to patch code
    // at all. The restore covers exactly the range that was changed.
    bool Write(mach_port_t task, uint64_t address, const void* src, uint64_t size,
               std::string* error);

    bool RegionOf(mach_port_t task, uint64_t address, Region* out, std::string* error);

    // Fills up to `capacity` regions, walking the address space from 0, and returns how many were
    // written. A caller that gets `capacity` back should ask again with more room. This is the
    // only entry point that fills Region::userTag -- see the implementation for why the tag costs
    // a second query nobody else needs.
    uint32_t EnumRegions(mach_port_t task, Region* out, uint32_t capacity);
}
