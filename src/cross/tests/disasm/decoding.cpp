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

// --- Milestone 6 task 3: the token stream -------------------------------------------------
//
// These assert on CLASSIFICATION, not on rendered text. A test that compared strings would pass
// just as well against re-lexing Capstone's printed output, which is the design the spec
// rejected: an immediate re-lexed out of text is an anonymous number and a register is a word.

namespace
{
    using TokenType = ZydisTokenizer::TokenType;

    bool hasType(const ZydisTokenizer::InstructionToken& tokens, const TokenType type)
    {
        for(const auto& token : tokens.tokens)
        {
            if(token.type == type)
                return true;
        }
        return false;
    }

    TokenType typeOfText(const ZydisTokenizer::InstructionToken& tokens, const QString& text)
    {
        for(const auto& token : tokens.tokens)
        {
            if(token.text == text)
                return token.type;
        }
        return TokenType::Last;
    }

    duint valueOfType(const ZydisTokenizer::InstructionToken& tokens, const TokenType type)
    {
        for(const auto& token : tokens.tokens)
        {
            if(token.type == type)
                return token.value.value;
        }
        return 0;
    }

    QString render(const ZydisTokenizer::InstructionToken& tokens)
    {
        QString out;
        for(const auto& token : tokens.tokens)
            out += token.text;
        return out;
    }
}

TEST_CASE("an arm64 load classifies its register, its memory operator and its immediate")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // ldr x0, [x1, #8]
    const uint8_t code[] = {0x20, 0x04, 0x40, 0xf9};
    const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("decoded: " << inst.instStr.toStdString() << " tokens: " << render(inst.tokens).toStdString());

    REQUIRE(typeOfText(inst.tokens, "x0") == TokenType::GeneralRegister);
    REQUIRE(typeOfText(inst.tokens, "x1") == TokenType::MemoryBaseRegister);
    REQUIRE(hasType(inst.tokens, TokenType::MemoryBrackets));
    REQUIRE(valueOfType(inst.tokens, TokenType::Address) == 8);
}

TEST_CASE("an arm64 stack pair is classified as a push or pop, not as an ordinary mnemonic")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // stp x29, x30, [sp, #-0x10]!  -- the prologue the spec names as an analysis heuristic.
    const uint8_t code[] = {0xfd, 0x7b, 0xbf, 0xa9};
    const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("tokens: " << render(inst.tokens).toStdString());
    REQUIRE(typeOfText(inst.tokens, "stp") == TokenType::MnemonicPushPop);
}

TEST_CASE("an x86-64 memory operand classifies its base, its scale and its displacement")
{
    X86Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // mov eax, dword ptr [rbx + rcx*4 + 0x10]
    const uint8_t code[] = {0x8b, 0x44, 0x8b, 0x10};
    const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("decoded: " << inst.instStr.toStdString() << " tokens: " << render(inst.tokens).toStdString());

    REQUIRE(typeOfText(inst.tokens, "eax") == TokenType::GeneralRegister);
    REQUIRE(typeOfText(inst.tokens, "rbx") == TokenType::MemoryBaseRegister);
    REQUIRE(typeOfText(inst.tokens, "rcx") == TokenType::MemoryIndexRegister);
    REQUIRE(hasType(inst.tokens, TokenType::MemoryScale));
    REQUIRE(valueOfType(inst.tokens, TokenType::Address) == 0x10);
}

TEST_CASE("an immediate carries its value, not only its text")
{
    X86Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // mov eax, 0x2a
    const uint8_t code[] = {0xb8, 0x2a, 0x00, 0x00, 0x00};
    const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);
    INFO("tokens: " << render(inst.tokens).toStdString());
    // The number is in the token. Recovering it from the text is the re-lexing this design
    // exists to avoid, so a token that renders "0x2a" and carries 0 would be a failure.
    REQUIRE(valueOfType(inst.tokens, TokenType::Value) == 0x2a);
}

TEST_CASE("a call and a return are classified by what they do, on both architectures")
{
    {
        X86Architecture architecture;
        QCapstone disassembler(0, &architecture);
        const uint8_t call[] = {0xe8, 0x00, 0x00, 0x00, 0x00};   // call +0
        const uint8_t ret[] = {0xc3};                             // ret
        REQUIRE(typeOfText(disassembler.DisassembleAt(call, sizeof(call), 0x1000, 0, false).tokens,
                           "call") == TokenType::MnemonicCall);
        REQUIRE(typeOfText(disassembler.DisassembleAt(ret, sizeof(ret), 0x1000, 0, false).tokens,
                           "ret") == TokenType::MnemonicRet);
    }
    {
        Arm64Architecture architecture;
        QCapstone disassembler(0, &architecture);
        const uint8_t bl[] = {0x01, 0x00, 0x00, 0x94};   // bl +4
        const uint8_t ret[] = {0xc0, 0x03, 0x5f, 0xd6};  // ret
        REQUIRE(typeOfText(disassembler.DisassembleAt(bl, sizeof(bl), 0x1000, 0, false).tokens,
                           "bl") == TokenType::MnemonicCall);
        REQUIRE(typeOfText(disassembler.DisassembleAt(ret, sizeof(ret), 0x1000, 0, false).tokens,
                           "ret") == TokenType::MnemonicRet);
    }
}

TEST_CASE("an instruction reports the registers it reads and writes")
{
    {
        Arm64Architecture architecture;
        QCapstone disassembler(0, &architecture);
        // ldr x0, [x1, #8] -- reads x1, writes x0.
        const uint8_t code[] = {0x20, 0x04, 0x40, 0xf9};
        const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);

        bool readsX1 = false;
        bool writesX0 = false;
        for(const auto& reg : inst.regsReferenced)
        {
            if(QString(reg.first) == "x1" && (reg.second & 1) != 0)
                readsX1 = true;
            if(QString(reg.first) == "x0" && (reg.second & 2) != 0)
                writesX0 = true;
        }
        INFO(inst.regsReferenced.size() << " registers referenced");
        REQUIRE(readsX1);
        REQUIRE(writesX0);
    }
    {
        X86Architecture architecture;
        QCapstone disassembler(0, &architecture);
        // mov eax, dword ptr [rbx + rcx*4 + 0x10] -- reads rbx and rcx, writes eax.
        const uint8_t code[] = {0x8b, 0x44, 0x8b, 0x10};
        const Instruction_t inst = disassembler.DisassembleAt(code, sizeof(code), 0x1000, 0, false);

        bool readsRbx = false;
        bool writesEax = false;
        for(const auto& reg : inst.regsReferenced)
        {
            if(QString(reg.first) == "rbx" && (reg.second & 1) != 0)
                readsRbx = true;
            if(QString(reg.first) == "eax" && (reg.second & 2) != 0)
                writesEax = true;
        }
        REQUIRE(readsRbx);
        REQUIRE(writesEax);
    }
}

// --- Milestone 6 task 4: branch and flow information ---------------------------------------

TEST_CASE("arm64 branches are classified, and only resolvable ones name a destination")
{
    Arm64Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // b.eq +8 | b +8 | bl +8 | br x0 | blr x1 | ret, assembled at 0x1000.
    const uint8_t code[] = {0x40, 0x00, 0x00, 0x54,   // b.eq  0x1008
                            0x02, 0x00, 0x00, 0x14,   // b     0x100c
                            0x02, 0x00, 0x00, 0x94,   // bl    0x1010
                            0x00, 0x00, 0x1f, 0xd6,   // br    x0
                            0x20, 0x00, 0x3f, 0xd6,   // blr   x1
                            0xc0, 0x03, 0x5f, 0xd6};  // ret

    const auto at = [&](duint rva) {
        return disassembler.DisassembleAt(code + rva, sizeof(code) - rva, 0x1000, rva, false);
    };

    const Instruction_t conditional = at(0);
    INFO("conditional: " << conditional.instStr.toStdString());
    REQUIRE(conditional.branchType == Instruction_t::Conditional);
    REQUIRE(conditional.branchDestination == 0x1008);

    const Instruction_t unconditional = at(4);
    REQUIRE(unconditional.branchType == Instruction_t::Unconditional);
    REQUIRE(unconditional.branchDestination == 0x100c);

    const Instruction_t call = at(8);
    REQUIRE(call.branchType == Instruction_t::Call);
    REQUIRE(call.branchDestination == 0x1010);

    // The gap, asserted so that nobody later fills it with a guess. `br xN` and `blr xN` are
    // everywhere in Apple code, and their target is a register value -- not knowable from the
    // instruction. Note that `blr` reports CS_GRP_BRANCH_RELATIVE even though it is indirect,
    // which is exactly why the destination is read from the operand type instead.
    const Instruction_t indirectJump = at(12);
    REQUIRE(indirectJump.branchType == Instruction_t::Unconditional);
    REQUIRE(indirectJump.branchDestination == 0);

    const Instruction_t indirectCall = at(16);
    REQUIRE(indirectCall.branchType == Instruction_t::Call);
    REQUIRE(indirectCall.branchDestination == 0);

    const Instruction_t ret = at(20);
    REQUIRE(ret.branchType == Instruction_t::None);
    REQUIRE(ret.branchDestination == 0);
}

TEST_CASE("x86-64 branches are classified, and only resolvable ones name a destination")
{
    X86Architecture architecture;
    QCapstone disassembler(0, &architecture);

    // je +0 | jmp +0 | call +0 | jmp rax | call rbx | ret, assembled at 0x1000.
    const uint8_t code[] = {0x74, 0x00,
                            0xeb, 0x00,
                            0xe8, 0x00, 0x00, 0x00, 0x00,
                            0xff, 0xe0,
                            0xff, 0xd3,
                            0xc3};

    const auto at = [&](duint rva) {
        return disassembler.DisassembleAt(code + rva, sizeof(code) - rva, 0x1000, rva, false);
    };

    const Instruction_t conditional = at(0);
    INFO("conditional: " << conditional.instStr.toStdString());
    REQUIRE(conditional.branchType == Instruction_t::Conditional);
    REQUIRE(conditional.branchDestination == 0x1002);

    const Instruction_t unconditional = at(2);
    REQUIRE(unconditional.branchType == Instruction_t::Unconditional);
    REQUIRE(unconditional.branchDestination == 0x1004);

    const Instruction_t call = at(4);
    REQUIRE(call.branchType == Instruction_t::Call);
    REQUIRE(call.branchDestination == 0x1009);

    const Instruction_t indirectJump = at(9);
    REQUIRE(indirectJump.branchType == Instruction_t::Unconditional);
    REQUIRE(indirectJump.branchDestination == 0);

    const Instruction_t indirectCall = at(11);
    REQUIRE(indirectCall.branchType == Instruction_t::Call);
    REQUIRE(indirectCall.branchDestination == 0);
}
