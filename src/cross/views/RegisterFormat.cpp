#include <views/RegisterFormat.h>

#include <cstdio>
#include <cstring>

namespace machdbg::views
{
    std::string FormatValue(const uint64_t value, const uint16_t bits)
    {
        // Digits, not bytes: a 16-bit register is four hex digits and a 64-bit one is sixteen. A
        // width taken from sizeof(uint64_t) instead would print cs as 000000000000002b and claim
        // a 64-bit register that does not exist. A descriptor with no width (0) is treated as
        // 64-bit rather than printed as nothing.
        const int digits = bits > 0 ? (bits + 3) / 4 : 16;
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%0*llx", digits,
                      static_cast<unsigned long long>(value));
        return buffer;
    }

    uint64_t ValueThroughDescriptor(const DbgRegisters& regs, const DbgRegisterDesc& desc)
    {
        const uint8_t* base = nullptr;
        switch(regs.arch)
        {
        case DbgArch_Arm64:
            base = reinterpret_cast<const uint8_t*>(&regs.arm64);
            break;
        case DbgArch_X86_64:
            base = reinterpret_cast<const uint8_t*>(&regs.x86_64);
            break;
        default:
            return 0;
        }

        uint64_t value = 0;
        std::memcpy(&value, base + desc.offset, sizeof(value));

        // Masked to the register's own width: the engine stores a 16-bit segment register in a
        // 64-bit field, and the upper bits are that field's padding, not the register's data.
        if(desc.bits > 0 && desc.bits < 64)
            value &= (1ull << desc.bits) - 1;
        return value;
    }
}
