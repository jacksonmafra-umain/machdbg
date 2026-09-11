#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <MachBug/macho/Reader.h>

// The Mach-O image, as data. This parser knows nothing about Mach: it is handed a Reader and an
// address, and everything it learns it learns by reading bytes. That is the layering the spec
// (section 8) makes a requirement, and Reader.h records the measurement behind it.
//
// What is read here is what milestone 5's views need: the segments, the UUID and where the image
// was linked to sit. LC_SYMTAB, LC_FUNCTION_STARTS and the chained fixups are later milestones --
// deliberately absent rather than parsed-and-unused, because a field nothing reads is a field
// nothing checks.
namespace MachBug::macho
{
    struct Segment
    {
        char name[17] = {};     // 16 from the load command, plus room for a terminator it may lack
        uint64_t vmAddr = 0;
        uint64_t vmSize = 0;
        uint64_t fileOffset = 0;
        uint32_t initProt = 0;  // VM_PROT_* bits
        uint32_t maxProt = 0;
    };

    struct Image
    {
        std::vector<Segment> segments;
        uint8_t uuid[16] = {};
        bool hasUuid = false;

        // Where __TEXT was linked to sit. The ASLR slide is the address the image actually
        // loaded at minus this, which is how a caller turns a link-time address into a runtime
        // one -- see core/Modules.
        uint64_t textVmAddr = 0;

        // The sum of the segments' vmSize: how much of the address space the image occupies,
        // which is what a memory map correlates a region against.
        uint64_t vmSize = 0;
    };

    // Parses the image at `imageAddress` through `reader` -- a file offset for a FileReader
    // (normally 0) and a load address for a MemoryReader. Returns false with a named diagnostic
    // on anything it will not vouch for.
    //
    // Every bound here exists because this parser reads the memory of processes that are being
    // debugged, which is the population most likely to contain a corrupt or hostile header: a
    // cmdsize of zero would loop forever, a cmdsize that overruns sizeofcmds would read past the
    // commands, and an ncmds of four billion would allocate until the debugger died rather than
    // the target.
    bool Parse(Reader& reader, uint64_t imageAddress, Image* out, std::string* error);
}
