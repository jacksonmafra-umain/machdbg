#pragma once

#include <Disassembler/Architecture.h>

struct LinuxArchitecture : Architecture
{
    [[nodiscard]] bool disasm64() const override { return true; }
    [[nodiscard]] bool addr64() const override { return true; }
};
