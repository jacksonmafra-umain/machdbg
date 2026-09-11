#include <MachBug/macho/Reader.h>

#include <MachBug/memory/Memory.h>

#include <cerrno>
#include <cstring>

namespace MachBug::macho
{
    FileReader::~FileReader()
    {
        if(mFile != nullptr)
            std::fclose(mFile);
    }

    bool FileReader::Open(const std::string& path, std::string* error)
    {
        if(mFile != nullptr)
            std::fclose(mFile);

        mFile = std::fopen(path.c_str(), "rb");
        if(mFile == nullptr)
        {
            if(error)
                *error = "cannot open " + path + ": " + std::strerror(errno);
            return false;
        }
        return true;
    }

    bool FileReader::Read(const uint64_t address, void* out, const uint64_t size)
    {
        if(mFile == nullptr || out == nullptr)
            return false;
        if(size == 0)
            return true;

        if(std::fseek(mFile, static_cast<long>(address), SEEK_SET) != 0)
            return false;
        return std::fread(out, 1, size, mFile) == size;
    }

    MemoryReader::MemoryReader(const mach_port_t task)
        : mTask(task)
    {
    }

    bool MemoryReader::Read(const uint64_t address, void* out, const uint64_t size)
    {
        if(size == 0)
            return true;
        // The diagnostic is dropped here rather than carried: this interface reports whether the
        // bytes arrived, and the parser turns a failed read into a diagnostic that says which
        // structure it was reading -- which is the more useful half of the sentence.
        return memory::Read(mTask, address, out, size, nullptr);
    }

    BufferReader::BufferReader(const uint8_t* bytes, const uint64_t size)
        : mBytes(bytes)
        , mSize(size)
    {
    }

    bool BufferReader::Read(const uint64_t address, void* out, const uint64_t size)
    {
        if(mBytes == nullptr || out == nullptr)
            return false;
        if(size == 0)
            return true;
        // Checked against the end rather than by adding: address + size can wrap, and a wrapped
        // bound accepts exactly the read it was meant to refuse.
        if(address > mSize || size > mSize - address)
            return false;

        std::memcpy(out, mBytes + address, size);
        return true;
    }
}
