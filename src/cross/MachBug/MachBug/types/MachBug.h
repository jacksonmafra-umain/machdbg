#pragma once

#include <cstdint>

namespace MachBug
{
    typedef uint8_t uint8;
    typedef uint16_t uint16;
    typedef uint32_t uint32;
    typedef uint64_t uint64;

    typedef uint64 ptr;

    // Mirrors ElfBug::Arch, extended with the two architectures the api contract already
    // reserves (DbgArch_Arm64, DbgArch_Arm64e in machbug_api.h) that ElfBug has no use for.
    enum class Arch : uint32_t
    {
        Unknown = 0,
        X86_64 = 1,
        I386 = 2,
        Arm64 = 3,
        Arm64e = 4,
    };
}
