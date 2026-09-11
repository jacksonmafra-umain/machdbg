#include <MachBug/macho/Image.h>

#include <mach-o/loader.h>

#include <cstdio>
#include <cstring>

namespace MachBug::macho
{
    namespace
    {
        std::string hex(const uint64_t value)
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "0x%llx",
                          static_cast<unsigned long long>(value));
            return buffer;
        }

        // No 32-bit images. The engine debugs 64-bit targets only (spec section 5: DbgArch_I386
        // has no register file), so a 32-bit header is refused rather than half-parsed into a
        // structure whose fields are all the wrong width.
        bool magicIsSupported(const uint32_t magic)
        {
            return magic == MH_MAGIC_64;
        }
    }

    bool Parse(Reader& reader, const uint64_t imageAddress, Image* const out, std::string* error)
    {
        if(out == nullptr)
            return false;
        *out = Image{};

        mach_header_64 header{};
        if(!reader.Read(imageAddress, &header, sizeof(header)))
        {
            if(error)
                *error = "could not read a Mach-O header at " + hex(imageAddress);
            return false;
        }

        if(!magicIsSupported(header.magic))
        {
            if(error)
            {
                *error = "not a 64-bit Mach-O image at " + hex(imageAddress) + ": its first "
                         "four bytes are " + hex(header.magic) + ", and this engine debugs "
                         "64-bit targets only";
            }
            return false;
        }

        // A header can claim any number of commands. Read them one at a time, bounded by the
        // byte count it also claims, so a wrong ncmds costs a failed read rather than a walk
        // through whatever follows.
        uint64_t offset = imageAddress + sizeof(mach_header_64);
        const uint64_t commandsEnd = offset + header.sizeofcmds;

        for(uint32_t i = 0; i < header.ncmds; ++i)
        {
            load_command command{};
            if(offset + sizeof(command) > commandsEnd ||
               !reader.Read(offset, &command, sizeof(command)))
            {
                if(error)
                    *error = "load command " + std::to_string(i) + " runs past the " +
                             std::to_string(header.sizeofcmds) + " bytes the header claims for "
                             "its commands";
                return false;
            }

            // The loop's own termination, and the reason it is checked rather than assumed: a
            // cmdsize of zero advances by nothing and reads the same command until something
            // kills the debugger. A cmdsize smaller than the header it introduces is the same
            // bug wearing a different number.
            if(command.cmdsize < sizeof(load_command) ||
               offset + command.cmdsize > commandsEnd)
            {
                if(error)
                    *error = "load command " + std::to_string(i) + " has a cmdsize of " +
                             std::to_string(command.cmdsize) + ", which does not advance past "
                             "itself -- refusing rather than reading it forever";
                return false;
            }

            if(command.cmd == LC_SEGMENT_64)
            {
                segment_command_64 segment{};
                if(command.cmdsize < sizeof(segment) ||
                   !reader.Read(offset, &segment, sizeof(segment)))
                {
                    if(error)
                        *error = "segment " + std::to_string(i) + " is shorter than a "
                                 "segment_command_64";
                    return false;
                }

                Segment parsed;
                // segname is 16 bytes and need not be terminated; the struct keeps 17 so the
                // copy always is.
                std::memcpy(parsed.name, segment.segname, sizeof(segment.segname));
                parsed.vmAddr = segment.vmaddr;
                parsed.vmSize = segment.vmsize;
                parsed.fileOffset = segment.fileoff;
                parsed.initProt = static_cast<uint32_t>(segment.initprot);
                parsed.maxProt = static_cast<uint32_t>(segment.maxprot);

                if(std::strcmp(parsed.name, SEG_TEXT) == 0)
                    out->textVmAddr = segment.vmaddr;
                out->vmSize += segment.vmsize;
                out->segments.push_back(parsed);
            }
            else if(command.cmd == LC_UUID)
            {
                uuid_command uuidCommand{};
                if(command.cmdsize >= sizeof(uuidCommand) &&
                   reader.Read(offset, &uuidCommand, sizeof(uuidCommand)))
                {
                    std::memcpy(out->uuid, uuidCommand.uuid, sizeof(out->uuid));
                    out->hasUuid = true;
                }
            }

            offset += command.cmdsize;
        }

        if(out->segments.empty())
        {
            // An image with no segments is not an image this engine can say anything about, and
            // reporting it as a successful parse would hand every caller an empty answer that
            // looks like a fact.
            if(error)
                *error = "the Mach-O image at " + hex(imageAddress) + " declares no segments";
            return false;
        }
        return true;
    }
}
