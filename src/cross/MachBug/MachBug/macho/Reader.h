#pragma once

#include <mach/mach.h>

#include <cstdint>
#include <cstdio>
#include <string>

// Where a Mach-O image's bytes come from. One method, deliberately: a reader that can also seek,
// or report its size, or map itself, is a reader whose implementations disagree about what those
// mean, and this interface exists to have exactly one meaning.
//
// THIS IS WHY THE PARSER DOES NOT TAKE A PATH (spec section 8). System dylibs live in the dyld
// shared cache and have no file on disk -- measured on this machine: `/usr/lib/libc++.1.dylib`
// and `/usr/lib/libSystem.B.dylib` are both loaded in every process and neither exists as a
// file. A parser that opens a path fails on the first `libSystem`, so process memory is not an
// alternative source here, it is the only one for most of what a debugger has to describe.
namespace MachBug::macho
{
    class Reader
    {
    public:
        virtual ~Reader() = default;

        // Fills `size` bytes at `address`, which means a file offset for a file and an address
        // in some task for memory. False on any short or failed read: a parser that accepts a
        // partial read walks into whatever was already in the buffer.
        virtual bool Read(uint64_t address, void* out, uint64_t size) = 0;
    };

    // An image in a file. Addresses are offsets from the start of the file, so a caller parsing
    // a file passes a load address of 0 -- there is nothing loaded.
    class FileReader : public Reader
    {
    public:
        ~FileReader() override;

        // False, with a named diagnostic, if the path cannot be opened -- which for a shared
        // cache dylib is the normal outcome and not a fault.
        bool Open(const std::string& path, std::string* error);

        bool Read(uint64_t address, void* out, uint64_t size) override;

    private:
        FILE* mFile = nullptr;
    };

    // An image in a live address space, read through MachBug::memory::Read so that every
    // mach_vm_* call in the engine still lives in one file (memory/Memory.cpp).
    class MemoryReader : public Reader
    {
    public:
        explicit MemoryReader(mach_port_t task);

        bool Read(uint64_t address, void* out, uint64_t size) override;

    private:
        mach_port_t mTask;
    };

    // An image already in this process's own memory, as bytes. For tests that need to hand the
    // parser something deliberately malformed -- which no file and no live process will reliably
    // provide, and which is exactly the input a debugger has to survive.
    class BufferReader : public Reader
    {
    public:
        BufferReader(const uint8_t* bytes, uint64_t size);

        bool Read(uint64_t address, void* out, uint64_t size) override;

    private:
        const uint8_t* mBytes;
        uint64_t mSize;
    };
}
