// Milestone 6 task 2: decoding, through the interface the disassembly view consumes.
//
// The fixtures are bytes this repository has already measured elsewhere, so a wrong answer is
// recognisable rather than merely different: `00 00 20 d4` and `cc` are milestone 4's arm64 and
// x86-64 software traps, verified with the assembler when breakpoints were built, and
// `fd 7b bf a9` is the arm64 prologue the spec names as an analysis heuristic.
#include <catch2/catch_test_macros.hpp>

#include <Disassembler/Architecture.h>
#include <Disassembler/QCapstone.h>

#include <cstdint>
#include <vector>

namespace
{
    struct Arm64Architecture : Architecture
    {
        bool disasm64() const override { return true; }
        bool addr64() const override { return true; }
        Cpu cpu() const override { return Cpu::Arm64; }
    };

    struct X86Architecture : Architecture
    {
        bool disasm64() const override { return true; }
        bool addr64() const override { return true; }
        Cpu cpu() const override { return Cpu::X86; }
    };
}

TEST_CASE("arm64 instructions decode to what the assembler produced")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    const uint8_t code[] = {0x00, 0x00, 0x20, 0xd4, 0xfd, 0x7b, 0xbf, 0xa9};

    const Instruction_t brk = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("first: " << brk.instStr.toStdString());
    REQUIRE(brk.length == 4);
    REQUIRE(brk.instStr.startsWith("brk"));
    REQUIRE(brk.dump.size() == 4);

    const Instruction_t stp = disassembler.DisassembleAt(code + 4, sizeof(code) - 4, 0x1000, 4,
                                                         false);
    INFO("second: " << stp.instStr.toStdString());
    REQUIRE(stp.length == 4);
    REQUIRE(stp.instStr.startsWith("stp"));
}

TEST_CASE("x86-64 instructions decode to what the assembler produced")
{
    X86Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // cc int3; 55 push rbp; 48 89 e5 mov rbp, rsp
    const uint8_t code[] = {0xcc, 0x55, 0x48, 0x89, 0xe5};

    const Instruction_t int3 = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("first: " << int3.instStr.toStdString());
    REQUIRE(int3.length == 1);
    REQUIRE(int3.instStr.startsWith("int3"));

    const Instruction_t push = disassembler.DisassembleAt(code + 1, sizeof(code) - 1, 0x1000, 1,
                                                          false);
    REQUIRE(push.length == 1);
    REQUIRE(push.instStr.startsWith("push"));

    const Instruction_t mov = disassembler.DisassembleAt(code + 2, sizeof(code) - 2, 0x1000, 2,
                                                         false);
    INFO("third: " << mov.instStr.toStdString());
    REQUIRE(mov.length == 3);
    REQUIRE(mov.instStr.startsWith("mov"));
}

TEST_CASE("bytes that decode to nothing still advance by at least one")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // Not a valid arm64 encoding. A length of zero here would make every walk over it loop
    // forever, and a debugger reads bytes like these constantly -- it is what data looks like
    // to a disassembler.
    const uint8_t rubbish[] = {0xff, 0xff, 0xff, 0xff};
    const Instruction_t inst = disassembler.DisassembleAt(rubbish, sizeof(rubbish), 0x2000, 0,
                                                          false);
    INFO("undecodable: " << inst.instStr.toStdString() << " length " << inst.length);
    REQUIRE(inst.length >= 1);
    REQUIRE_FALSE(inst.instStr.isEmpty());
}

TEST_CASE("the forward walk lands on instruction boundaries")
{
    X86Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // cc | 55 | 48 89 e5 | 90 -- boundaries at 0, 1, 2, 5, 6.
    const uint8_t code[] = {0xcc, 0x55, 0x48, 0x89, 0xe5, 0x90};

    REQUIRE(disassembler.DisassembleNext(code, 0x1000, sizeof(code), 0, 1) == 1);
    REQUIRE(disassembler.DisassembleNext(code, 0x1000, sizeof(code), 0, 2) == 2);
    REQUIRE(disassembler.DisassembleNext(code, 0x1000, sizeof(code), 0, 3) == 5);
}

TEST_CASE("the backward walk terminates on undecodable bytes")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // Every four-byte group here is invalid, so the walk is driven entirely by the failure
    // stride. The property under test is that it returns at all.
    std::vector<uint8_t> rubbish(256, 0xff);
    const ulong landed = disassembler.DisassembleBack(rubbish.data(), 0x3000, rubbish.size(),
                                                       200, 4);
    INFO("landed at " << landed);
    REQUIRE(landed <= 200);
}
