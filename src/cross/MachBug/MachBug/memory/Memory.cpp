#include <MachBug/memory/Memory.h>

#include <mach/mach_error.h>
#include <mach/mach_vm.h>

namespace MachBug::memory
{
    namespace
    {
        bool guard(mach_port_t task, uint64_t size, std::string* error)
        {
            if(task == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no task port: the engine is not attached to a target";
                return false;
            }
            if(size == 0)
            {
                if(error)
                    *error = "zero-length memory operation";
                return false;
            }
            return true;
        }

        std::string hex(const uint64_t value)
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
            return buffer;
        }

        // Two flavors, because neither one answers everything Region declares:
        // VM_REGION_BASIC_INFO_64 carries protection *and* max_protection but no user tag, while
        // VM_REGION_EXTENDED_INFO carries the tag and no maximum. `wantTag` is what lets a caller
        // pay for the second call only when the tag will actually be shown -- the write path and
        // RegionOf() need protection and nothing else; a memory map needs tags.
        bool regionAt(mach_port_t task, mach_vm_address_t address, Region* out, bool wantTag,
                      std::string* error)
        {
            mach_vm_address_t base = address;
            mach_vm_size_t size = 0;
            vm_region_basic_info_data_64_t basic{};
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object = MACH_PORT_NULL;
            const kern_return_t kr = mach_vm_region(task, &base, &size, VM_REGION_BASIC_INFO_64,
                reinterpret_cast<vm_region_info_t>(&basic), &count, &object);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = "no mapped region at or above " + hex(address) + ": " +
                             mach_error_string(kr);
                return false;
            }
            out->base = base;
            out->size = size;
            out->protection = static_cast<uint32_t>(basic.protection);
            out->maxProtection = static_cast<uint32_t>(basic.max_protection);
            out->userTag = 0;

            if(wantTag)
            {
                mach_vm_address_t tagBase = base;
                mach_vm_size_t tagSize = 0;
                vm_region_extended_info_data_t extended{};
                mach_msg_type_number_t extendedCount = VM_REGION_EXTENDED_INFO_COUNT;
                mach_port_t tagObject = MACH_PORT_NULL;
                if(mach_vm_region(task, &tagBase, &tagSize, VM_REGION_EXTENDED_INFO,
                                  reinterpret_cast<vm_region_info_t>(&extended), &extendedCount,
                                  &tagObject) == KERN_SUCCESS && tagBase == base)
                {
                    out->userTag = extended.user_tag;
                }
                // A tag that could not be read stays 0 rather than failing the whole query: the
                // tag is descriptive, and a region with base, size and protection is still a
                // usable answer without it.
            }
            return true;
        }
    }

    bool Read(const mach_port_t task, const uint64_t address, void* dest, const uint64_t size,
              std::string* error)
    {
        if(!guard(task, size, error))
            return false;
        if(!dest)
        {
            if(error)
                *error = "no destination buffer to read into";
            return false;
        }

        mach_vm_size_t read = 0;
        const kern_return_t kr = mach_vm_read_overwrite(task, address, size,
            reinterpret_cast<mach_vm_address_t>(dest), &read);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = "reading " + std::to_string(size) + " byte(s) at " + hex(address) +
                         " failed: " + mach_error_string(kr);
            return false;
        }
        if(read != size)
        {
            if(error)
                *error = "short read at " + hex(address) + ": asked for " + std::to_string(size) +
                         " byte(s), got " + std::to_string(read);
            return false;
        }
        return true;
    }

    bool Write(const mach_port_t task, const uint64_t address, const void* src,
               const uint64_t size, std::string* error)
    {
        if(!guard(task, size, error))
            return false;
        if(!src)
        {
            if(error)
                *error = "no source buffer to write from";
            return false;
        }

        const auto attempt = [&]() {
            return mach_vm_write(task, address, reinterpret_cast<vm_offset_t>(const_cast<void*>(src)),
                                 static_cast<mach_msg_type_number_t>(size));
        };

        kern_return_t kr = attempt();
        if(kr == KERN_SUCCESS)
            return true;

        // The measured failure for a read-execute page is KERN_INVALID_ADDRESS, not
        // KERN_PROTECTION_FAILURE -- so the flip is attempted for any failure rather than gated
        // on the code that "should" mean unwritable. A genuinely unmapped address then fails
        // regionAt() below and is reported as having no mapped region, which is what actually
        // went wrong.
        Region region{};
        if(!regionAt(task, address, &region, false, error))
            return false;

        const kern_return_t flipped = mach_vm_protect(task, region.base, region.size, FALSE,
            VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        if(flipped != KERN_SUCCESS)
        {
            if(error)
                *error = "cannot make " + std::to_string(size) + " byte(s) at " + hex(address) +
                         " writable: " + mach_error_string(flipped) + " (the page's protection is " +
                         std::to_string(region.protection) + ")";
            return false;
        }

        kr = attempt();

        // Restored whether or not the write succeeded, and for exactly the range that was
        // flipped: leaving a target's code page writable is a change to the target this call was
        // never asked to make.
        const kern_return_t restored = mach_vm_protect(task, region.base, region.size, FALSE,
                                                       region.protection);

        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = "writing " + std::to_string(size) + " byte(s) at " + hex(address) +
                         " failed even after making the page writable: " + mach_error_string(kr);
            return false;
        }
        if(restored != KERN_SUCCESS && error)
        {
            *error = std::string("the write succeeded but the page's original protection could "
                                 "not be restored: ") + mach_error_string(restored);
        }
        return true;
    }

    bool RegionOf(const mach_port_t task, const uint64_t address, Region* out, std::string* error)
    {
        if(!out)
        {
            if(error)
                *error = "no Region to fill";
            return false;
        }
        if(!guard(task, 1, error))
            return false;
        return regionAt(task, address, out, false, error);
    }

    uint32_t EnumRegions(const mach_port_t task, Region* out, const uint32_t capacity)
    {
        if(task == MACH_PORT_NULL || !out || capacity == 0)
            return 0;

        uint32_t written = 0;
        mach_vm_address_t address = 0;
        while(written < capacity)
        {
            Region region{};
            std::string ignored;
            // The tag is asked for here and nowhere else: enumeration is what feeds a memory map.
            if(!regionAt(task, address, &region, true, &ignored))
                break;
            out[written++] = region;
            const mach_vm_address_t next = region.base + region.size;
            if(next <= address) // No forward progress: stop rather than spin.
                break;
            address = next;
        }
        return written;
    }
}
