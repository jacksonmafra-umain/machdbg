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
        uint32_t depth = 0;           // 0 for the task's own map, deeper inside a submap
        bool isSubmap = false;

        // A readable name for userTag, from static storage -- never null, and never "unknown"
        // for tag 0, which means untagged rather than unrecognised. See TagName().
        const char* tagName = "";
    };

    // What a user tag means, as words. MEASURED before this table was written, because the
    // taxonomy is not Windows's and guessing at it produces a map that reads plausibly and says
    // the wrong thing: a process's own __TEXT comes back with tag 0, its stack with 30
    // (VM_MEMORY_STACK) and a small malloc with 7 (VM_MEMORY_MALLOC_TINY).
    //
    // Tag 0 is therefore NOT "unknown". It is what ordinary mapped file content carries,
    // including the executable a user is looking at, and labelling it unknown would put the
    // most important row in the map under the least informative name.
    const char* TagName(uint32_t tag);

    bool Read(mach_port_t task, uint64_t address, void* dest, uint64_t size, std::string* error);

    // Writes into the target, flipping protection when the page cannot be written as it stands:
    // mach_vm_protect(VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY), write, restore. VM_PROT_COPY is
    // the part that matters for a code page -- it asks for a private copy rather than write
    // access to the shared, signed mapping, which is the only way a debugger gets to patch code
    // at all. The restore covers exactly the range that was changed.
    bool Write(mach_port_t task, uint64_t address, const void* src, uint64_t size,
               std::string* error);

    bool RegionOf(mach_port_t task, uint64_t address, Region* out, std::string* error);

    // Fills up to `capacity` regions, walking the address space from 0, and returns how many
    // were written. A caller that gets `capacity` back should ask again with more room.
    //
    // Walks with mach_vm_region_recurse rather than mach_vm_region, which is what reports
    // submaps at all -- and the shared cache, which is most of what any process maps, is behind
    // one. The recursive call also returns the user tag in the same reply, so the tag no longer
    // costs the second query the old walk needed.
    uint32_t EnumRegions(mach_port_t task, Region* out, uint32_t capacity);
}
