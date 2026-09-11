#pragma once

#include <mach/mach.h>

#include <cstdint>
#include <string>
#include <vector>

// What dyld has loaded into the target: which images, where each one landed, and by how much it
// moved from where it was linked.
//
// Read out of the target rather than asked of it. task_info(TASK_DYLD_INFO) hands back an
// address -- `all_image_info_addr` -- that belongs to the TARGET's address space, so every field
// after that is a memory::Read and nothing here is ever dereferenced. A debugger that forgot
// that would read its own dyld and report its own libraries as the target's, which is worse than
// failing: it answers.
//
// MEASURED LAYOUT (2026-09-11, macOS 26.x, dyld_all_image_infos version 17), and the fields are
// read BY OFFSET rather than with a struct, because the struct grows with its version and a
// future OS adding a field would silently shift everything a debugger reads:
//
//     dyld_all_image_infos: version @0 (uint32), infoArrayCount @4 (uint32),
//                           infoArray @8 (pointer), notification @16 (pointer); 368 bytes
//     dyld_image_info:      24 bytes -- imageLoadAddress @0, imageFilePath @8
//
// Live load and unload events are found by diffing this at each stop, the same way core/Threads
// finds new threads, and for the same reason: macOS notifies a debugger of neither. The spec
// (section 8) describes an internal breakpoint on dyld's `notification` function pointer as the
// alternative; it is deferred deliberately -- see the milestone 5 plan's decision 2 -- because a
// breakpoint the user never set has to stay invisible in the breakpoint list, in the hardware
// slot count, and across every detach.
namespace MachBug
{
    class Modules
    {
    public:
        struct Image
        {
            uint64_t loadAddress = 0;
            uint64_t slide = 0;   // loadAddress - the __TEXT vmaddr the image was linked at
            uint64_t size = 0;    // the sum of its segments' vmSize
            std::string path;
        };

        // Everything loaded right now, in dyld's own order (the main executable first). Returns
        // false with a named diagnostic when the target cannot be read at all; an empty list
        // with a true return means dyld was mid-update, which is an ordinary moment rather than
        // a failure -- see the comment on the implementation.
        //
        // MEASURED, and a caller has to expect it: AT THE FIRST STOP THE LIST IS EMPTY. That
        // stop is dyld's own notification trap, before the image array has been published, so a
        // debugger asking there -- which is exactly when a startup routine would ask -- gets
        // "nothing yet". The images appear once the target has run.
        static bool Enumerate(mach_port_t task, std::vector<Image>* out, std::string* error);
    };
}
