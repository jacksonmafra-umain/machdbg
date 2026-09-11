#include <MachBug/core/Modules.h>

#include <mach/mach_error.h>
#include <mach/task_info.h>

#include <MachBug/macho/Image.h>
#include <MachBug/macho/Reader.h>
#include <MachBug/memory/Memory.h>

namespace MachBug
{
    namespace
    {
        // Offsets into dyld_all_image_infos and dyld_image_info, measured rather than taken from
        // the struct -- see the header for why, and for the numbers.
        constexpr uint64_t kInfoArrayCountOffset = 4;
        constexpr uint64_t kInfoArrayOffset = 8;
        constexpr uint64_t kImageInfoStride = 24;
        constexpr uint64_t kImagePathOffset = 8;

        // A path is a pointer into the target with no length beside it, so it is read forward
        // until a NUL. In chunks, because one byte at a time is one mach_vm_read per character
        // and a path is read for every image at every stop.
        constexpr uint64_t kPathChunk = 64;

        // More images than any real process has. A count read out of a target that is mid-write
        // -- or lying -- would otherwise size an allocation, and the process that dies would be
        // the debugger rather than the one being debugged.
        constexpr uint32_t kMaxImages = 8192;

        std::string readCString(const mach_port_t task, const uint64_t address)
        {
            if(address == 0)
                return {};

            std::string out;
            for(uint64_t offset = 0; offset < 4096; offset += kPathChunk)
            {
                char chunk[kPathChunk]{};
                if(!memory::Read(task, address + offset, chunk, sizeof(chunk), nullptr))
                {
                    // A path that straddles the end of its mapping fails the whole chunk, so a
                    // shorter read is retried one byte at a time before giving up on it.
                    for(uint64_t i = 0; i < kPathChunk; ++i)
                    {
                        char byte = 0;
                        if(!memory::Read(task, address + offset + i, &byte, 1, nullptr))
                            return out;
                        if(byte == '\0')
                            return out;
                        out.push_back(byte);
                    }
                    continue;
                }

                for(const char byte : chunk)
                {
                    if(byte == '\0')
                        return out;
                    out.push_back(byte);
                }
            }
            return out;
        }
    }

    bool Modules::Enumerate(const mach_port_t task, std::vector<Image>* const out,
                            std::string* error)
    {
        if(out == nullptr)
            return false;
        out->clear();

        if(task == MACH_PORT_NULL)
        {
            if(error)
                *error = "no target to enumerate modules in";
            return false;
        }

        task_dyld_info_data_t info{};
        mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
        const kern_return_t kr = task_info(task, TASK_DYLD_INFO,
                                           reinterpret_cast<task_info_t>(&info), &count);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("task_info(TASK_DYLD_INFO) failed: ") + mach_error_string(kr);
            return false;
        }
        if(info.all_image_info_addr == 0)
        {
            if(error)
                *error = "the target reports no dyld image info address, so nothing is loaded "
                         "that this engine can see -- a target stopped before dyld ran at all";
            return false;
        }

        uint32_t imageCount = 0;
        uint64_t arrayAddress = 0;
        if(!memory::Read(task, info.all_image_info_addr + kInfoArrayCountOffset, &imageCount,
                         sizeof(imageCount), error) ||
           !memory::Read(task, info.all_image_info_addr + kInfoArrayOffset, &arrayAddress,
                         sizeof(arrayAddress), error))
        {
            return false;
        }

        // DOCUMENTED, not defensive: dyld sets infoArray to NULL while it is modifying the array
        // (mach-o/dyld_images.h:52). Answering "nothing changed, ask again at the next stop" is
        // right; reporting an error would report one during any dlopen a stop happened to
        // interrupt.
        if(arrayAddress == 0)
            return true;

        if(imageCount > kMaxImages)
        {
            if(error)
                *error = "the target claims " + std::to_string(imageCount) + " loaded images, "
                         "which is more than any real process has -- refusing to allocate for it";
            return false;
        }

        out->reserve(imageCount);
        for(uint32_t i = 0; i < imageCount; ++i)
        {
            const uint64_t entry = arrayAddress + static_cast<uint64_t>(i) * kImageInfoStride;

            uint64_t loadAddress = 0;
            uint64_t pathAddress = 0;
            if(!memory::Read(task, entry, &loadAddress, sizeof(loadAddress), nullptr) ||
               !memory::Read(task, entry + kImagePathOffset, &pathAddress, sizeof(pathAddress),
                             nullptr))
            {
                // One unreadable entry does not lose the rest: the array is being read out of a
                // process that is free to unload something between two of these reads.
                continue;
            }
            if(loadAddress == 0)
                continue;

            Image image;
            image.loadAddress = loadAddress;
            image.path = readCString(task, pathAddress);

            // The slide, which is the reason a module list is worth having at all: without it
            // every address in the image is a link-time address that means nothing at runtime.
            // Read from the image itself rather than computed from anything dyld reports, so a
            // wrong answer here is a wrong parse rather than a wrong subtraction.
            macho::MemoryReader reader(task);
            macho::Image parsed;
            if(macho::Parse(reader, loadAddress, &parsed, nullptr))
            {
                image.slide = loadAddress - parsed.textVmAddr;
                image.size = parsed.vmSize;
            }

            out->push_back(std::move(image));
        }
        return true;
    }

    Modules::Change Modules::Refresh(const mach_port_t task)
    {
        Change change;

        std::vector<Image> current;
        if(!Enumerate(task, &current, nullptr))
            return change;   // unreadable target: nothing changed that this engine can claim

        // See the header: an empty read is dyld not having published yet, which is the state
        // the very first stop is always in. Taking it as the baseline would turn the target's
        // whole launch set into load events.
        if(current.empty())
            return change;

        if(!mHaveBaseline)
        {
            mKnown = std::move(current);
            mHaveBaseline = true;
            return change;
        }

        const auto heldAt = [](const std::vector<Image>& images, const uint64_t address) {
            for(const Image& image : images)
            {
                if(image.loadAddress == address)
                    return true;
            }
            return false;
        };

        for(const Image& image : current)
        {
            if(!heldAt(mKnown, image.loadAddress))
                change.appeared.push_back(image);
        }
        for(const Image& image : mKnown)
        {
            if(!heldAt(current, image.loadAddress))
                change.disappeared.push_back(image);
        }

        mKnown = std::move(current);
        return change;
    }

    std::vector<Modules::Image> Modules::Known() const
    {
        return mKnown;
    }

    void Modules::Reset()
    {
        mKnown.clear();
        mHaveBaseline = false;
    }
}
