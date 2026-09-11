// Milestone 5 task 1: the Mach-O parser, and the reason it reads through an interface instead of
// opening a path.
//
// MEASURED, and the whole layering decision rests on it:
//
//     $ ls /usr/lib/libc++.1.dylib /usr/lib/libSystem.B.dylib
//     ls: /usr/lib/libc++.1.dylib: No such file or directory
//     ls: /usr/lib/libSystem.B.dylib: No such file or directory
//
// Both are loaded in every process on this machine, with real load addresses in dyld's image
// array. They live in the dyld shared cache and have no file behind them, so "open the path and
// parse it" fails on the first system library -- which is every process there is. The last test
// in this file is that case, and it asserts the open fails as well as that the memory parse
// works, so the file says out loud why the memory path is not optional.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <MachBug/macho/Image.h>
#include <MachBug/macho/Reader.h>

namespace
{
    // This test binary's own path, from dyld rather than from argv: the tests run from whatever
    // directory CTest picked, and a relative argv[0] would make the file case depend on that.
    std::string ownPath()
    {
        char buffer[4096]{};
        uint32_t size = sizeof(buffer);
        if(_NSGetExecutablePath(buffer, &size) != 0)
            return {};
        return buffer;
    }

    // This test binary's own load address, which is where the same image sits in memory.
    uint64_t ownLoadAddress()
    {
        for(uint32_t i = 0; i < _dyld_image_count(); ++i)
        {
            if(ownPath() == _dyld_get_image_name(i))
                return reinterpret_cast<uint64_t>(_dyld_get_image_header(i));
        }
        return 0;
    }

    // A loaded image whose path names no file. There is always at least one -- every process
    // links something out of the shared cache -- and the test says so rather than hardcoding
    // libc++, whose name is not a contract.
    struct CacheImage
    {
        std::string path;
        uint64_t loadAddress = 0;
    };

    CacheImage aSharedCacheImage()
    {
        for(uint32_t i = 0; i < _dyld_image_count(); ++i)
        {
            const char* const name = _dyld_get_image_name(i);
            if(name == nullptr)
                continue;
            if(FILE* file = std::fopen(name, "rb"))
            {
                std::fclose(file);
                continue;   // it has a file, so it is not the case under test
            }
            return {name, reinterpret_cast<uint64_t>(_dyld_get_image_header(i))};
        }
        return {};
    }

    std::vector<std::string> segmentNames(const MachBug::macho::Image& image)
    {
        std::vector<std::string> names;
        names.reserve(image.segments.size());
        for(const MachBug::macho::Segment& segment : image.segments)
            names.emplace_back(segment.name);
        return names;
    }

    bool contains(const std::vector<std::string>& names, const std::string& wanted)
    {
        for(const std::string& name : names)
        {
            if(name == wanted)
                return true;
        }
        return false;
    }
}

TEST_CASE("a Mach-O image parses from a file on disk")
{
    const std::string path = ownPath();
    INFO("own path: " << path);
    REQUIRE_FALSE(path.empty());

    MachBug::macho::FileReader reader;
    std::string error;
    REQUIRE(reader.Open(path, &error));
    INFO("diagnostic: " << error);

    MachBug::macho::Image image;
    REQUIRE(MachBug::macho::Parse(reader, 0, &image, &error));

    const std::vector<std::string> names = segmentNames(image);
    INFO("segments: " << image.segments.size());
    REQUIRE(contains(names, "__TEXT"));
    REQUIRE(image.textVmAddr != 0);

    // A UUID is not guaranteed by the format, but every binary this toolchain links has one, and
    // a parser that silently reported none would look identical to one that never read LC_UUID.
    REQUIRE(image.hasUuid);
}

TEST_CASE("the same image parses from live memory, and agrees with the file")
{
    const std::string path = ownPath();
    const uint64_t loadAddress = ownLoadAddress();
    INFO("own image at 0x" << std::hex << loadAddress);
    REQUIRE(loadAddress != 0);

    std::string error;
    MachBug::macho::FileReader file;
    REQUIRE(file.Open(path, &error));
    MachBug::macho::Image fromDisk;
    REQUIRE(MachBug::macho::Parse(file, 0, &fromDisk, &error));

    // mach_task_self(): the parser is being asked to read a live address space, and this process
    // is the one address space a test can rely on being there.
    MachBug::macho::MemoryReader memory(mach_task_self());
    MachBug::macho::Image fromMemory;
    REQUIRE(MachBug::macho::Parse(memory, loadAddress, &fromMemory, &error));
    INFO("diagnostic: " << error);

    // The same image, so the same segments and the same UUID. The vmaddrs agree too: they are
    // what the image was linked at, and the slide is the difference between that and where it
    // actually landed -- which is task 2's subject, not this one's.
    REQUIRE(segmentNames(fromMemory) == segmentNames(fromDisk));
    REQUIRE(fromMemory.textVmAddr == fromDisk.textVmAddr);
    REQUIRE(fromMemory.hasUuid == fromDisk.hasUuid);
    REQUIRE(std::memcmp(fromMemory.uuid, fromDisk.uuid, sizeof(fromMemory.uuid)) == 0);
}

TEST_CASE("something that is not Mach-O is refused by name")
{
    MachBug::macho::FileReader reader;
    std::string error;
    // A file that certainly exists and certainly is not Mach-O.
    REQUIRE(reader.Open("/etc/hosts", &error));

    MachBug::macho::Image image;
    REQUIRE_FALSE(MachBug::macho::Parse(reader, 0, &image, &error));
    INFO("diagnostic: " << error);
    // Refused with a reason, not accepted with nothing in it: an empty segment list is what a
    // parser that gave up silently produces, and a caller cannot tell that from a real image
    // that happens to have none.
    REQUIRE(error.find("Mach-O") != std::string::npos);
    REQUIRE(image.segments.empty());
}

TEST_CASE("a shared cache dylib parses from memory, and cannot be opened at all")
{
    const CacheImage cached = aSharedCacheImage();
    INFO("shared cache image: " << cached.path << " at 0x" << std::hex << cached.loadAddress);
    REQUIRE_FALSE(cached.path.empty());
    REQUIRE(cached.loadAddress != 0);

    // The half that makes the interface necessary rather than tidy: there is no file.
    std::string error;
    MachBug::macho::FileReader file;
    REQUIRE_FALSE(file.Open(cached.path, &error));
    INFO("open diagnostic: " << error);

    MachBug::macho::MemoryReader memory(mach_task_self());
    MachBug::macho::Image image;
    REQUIRE(MachBug::macho::Parse(memory, cached.loadAddress, &image, &error));
    INFO("parse diagnostic: " << error);
    REQUIRE(contains(segmentNames(image), "__TEXT"));
}

TEST_CASE("a load command that would loop forever is refused")
{
    // A header that claims one load command, whose cmdsize is zero. A walk that trusts cmdsize
    // advances by nothing and reads the same command until the process is killed -- and a
    // debugger reads exactly this kind of input from exactly the processes most worth debugging.
    std::vector<uint8_t> bytes(sizeof(mach_header_64) + sizeof(load_command), 0);
    auto* header = reinterpret_cast<mach_header_64*>(bytes.data());
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->filetype = MH_EXECUTE;
    header->ncmds = 1;
    header->sizeofcmds = sizeof(load_command);
    auto* command = reinterpret_cast<load_command*>(bytes.data() + sizeof(mach_header_64));
    command->cmd = LC_SEGMENT_64;
    command->cmdsize = 0;

    MachBug::macho::BufferReader reader(bytes.data(), bytes.size());
    MachBug::macho::Image image;
    std::string error;
    REQUIRE_FALSE(MachBug::macho::Parse(reader, 0, &image, &error));
    INFO("diagnostic: " << error);
    REQUIRE(error.find("cmdsize") != std::string::npos);
}
