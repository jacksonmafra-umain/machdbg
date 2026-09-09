// Milestone 3 task 6: the register presentation rules that need no Qt, asserted in the engine's
// own test binary. The widget itself is proven by task 7's accessibility check against a running
// app; what is unit-testable without an event loop is how wide a value prints and which bytes a
// descriptor addresses -- and the second of those is the rule a view has no way to check for
// itself.
#include <catch2/catch_test_macros.hpp>

#include <MachBug/arch/Arch.h>
#include <views/RegisterFormat.h>

TEST_CASE("a register's value is printed at its own width")
{
    REQUIRE(machdbg::views::FormatValue(0xFEEDull, 64) == "000000000000feed");
    REQUIRE(machdbg::views::FormatValue(0xFEEDull, 32) == "0000feed");
    REQUIRE(machdbg::views::FormatValue(0x2Bull, 16) == "002b");
    REQUIRE(machdbg::views::FormatValue(0x0ull, 64) == "0000000000000000");

    // A descriptor with no declared width is printed as 64-bit rather than as nothing.
    REQUIRE(machdbg::views::FormatValue(0xFEEDull, 0) == "000000000000feed");
}

TEST_CASE("a register is read through its descriptor, not its struct field")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0x123456789ABCull;
    regs.arm64.x[9] = 0xFEEDFACEull;

    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Descriptors(DbgArch_Arm64, &count);
    REQUIRE(descs != nullptr);

    bool checkedPc = false;
    bool checkedX9 = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        if((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0)
        {
            REQUIRE(machdbg::views::ValueThroughDescriptor(regs, descs[i]) == 0x123456789ABCull);
            checkedPc = true;
        }
        if(std::string(descs[i].name) == "x9")
        {
            REQUIRE(machdbg::views::ValueThroughDescriptor(regs, descs[i]) == 0xFEEDFACEull);
            checkedX9 = true;
        }
    }
    REQUIRE(checkedPc);
    REQUIRE(checkedX9);
}

TEST_CASE("a narrow register reads only its own bits")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_X86_64;
    // The engine stores a 16-bit segment register in a 64-bit field. Whatever the upper bits
    // hold is that field's padding, and a view that showed them would be showing the register
    // something it does not contain.
    regs.x86_64.cs = 0xFFFF002Bull & 0xFFFF;
    regs.x86_64.rip = 0xDEADBEEFull;

    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Descriptors(DbgArch_X86_64, &count);
    REQUIRE(descs != nullptr);

    bool checked = false;
    for(uint32_t i = 0; i < count; ++i)
    {
        if(std::string(descs[i].name) != "cs")
            continue;
        REQUIRE(descs[i].bits == 16);
        REQUIRE(machdbg::views::ValueThroughDescriptor(regs, descs[i]) == 0x2Bull);
        checked = true;
    }
    REQUIRE(checked);
}

TEST_CASE("a descriptor for the wrong architecture reads nothing rather than garbage")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_Unknown;

    uint32_t count = 0;
    const DbgRegisterDesc* descs = MachBug::arch::Descriptors(DbgArch_Arm64, &count);
    REQUIRE(descs != nullptr);
    REQUIRE(count > 0);
    REQUIRE(machdbg::views::ValueThroughDescriptor(regs, descs[0]) == 0);
}
