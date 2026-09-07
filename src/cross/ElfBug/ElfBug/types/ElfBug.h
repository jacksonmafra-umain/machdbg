#pragma once

#include <cstdint>

namespace ElfBug
{
    typedef uint8_t uint8;
    typedef uint16_t uint16;
    typedef uint32_t uint32;
    typedef uint64_t uint64;

    typedef uint64 ptr;

#define ElfBugArchValue(x32value, x64value) (x64value)

    enum class Arch : uint32_t
    {
        Unknown = 0,
        X86_64 = 1,
        I386 = 2,
    };
}
