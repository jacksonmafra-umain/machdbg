#include "CapstoneTokenizer.h"

#include "Architecture.h"
#include "CapstoneVersion.h"

#include <capstone/arm64.h>
#include <capstone/x86.h>

CapstoneTokenizer::CapstoneTokenizer(int maxModuleSize, Architecture* architecture)
    : mArchitecture(architecture)
    , mMaxModuleSize(maxModuleSize)
{
}

void CapstoneTokenizer::emitToken(const TokenType type, const QString& text, InstructionToken& out)
{
    out.tokens.emplace_back(type, text, TokenValue());
}

void CapstoneTokenizer::emitValue(const TokenType type, const duint value, const int size,
                                  InstructionToken& out)
{
    // The value goes in the token, not only its rendering. A highlighter can colour text; a
    // "follow this address" or "copy this immediate" needs the number, and recovering it from
    // the text is exactly the re-lexing this design exists to avoid.
    const QString text = QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 0, 16);
    out.tokens.emplace_back(type, text, TokenValue(size, value));
}

CapstoneTokenizer::TokenType CapstoneTokenizer::registerTypeOf(const QString& name)
{
    if(name.isEmpty())
        return TokenType::Uncategorized;

    const QChar first = name.at(0).toLower();
    if(name.startsWith(QLatin1String("zmm"), Qt::CaseInsensitive))
        return TokenType::ZmmRegister;
    if(name.startsWith(QLatin1String("ymm"), Qt::CaseInsensitive))
        return TokenType::YmmRegister;
    if(name.startsWith(QLatin1String("xmm"), Qt::CaseInsensitive))
        return TokenType::XmmRegister;
    if(name.startsWith(QLatin1String("mm"), Qt::CaseInsensitive))
        return TokenType::MmxRegister;
    if(name.startsWith(QLatin1String("st"), Qt::CaseInsensitive))
        return TokenType::FpuRegister;

    // arm64's v/q/d/s/h/b registers are the SIMD and floating-point file. They are mapped onto
    // the Xmm token type rather than given one of their own: the type list is x86's, this
    // milestone is not the place to extend it, and a SIMD register coloured as a SIMD register
    // is the property a reader actually wants.
    if(first == QLatin1Char('v') || first == QLatin1Char('q') || first == QLatin1Char('d') ||
       first == QLatin1Char('s') || first == QLatin1Char('h'))
    {
        bool numeric = false;
        name.mid(1).toInt(&numeric);
        if(numeric)
            return TokenType::XmmRegister;
    }

    return TokenType::GeneralRegister;
}

void CapstoneTokenizer::emitRegister(const csh handle, const unsigned reg, const TokenType type,
                                     InstructionToken& out) const
{
    const char* const name = cs_reg_name(handle, reg);
    if(name == nullptr)
    {
        emitToken(TokenType::Uncategorized, QStringLiteral("?"), out);
        return;
    }

    const QString text = QString::fromUtf8(name);
    emitToken(type == TokenType::Uncategorized ? registerTypeOf(text) : type, text, out);
}

void CapstoneTokenizer::emitMnemonic(const cs_insn& insn, InstructionToken& out) const
{
    const QString mnemonic = QString::fromUtf8(insn.mnemonic);
    TokenType type = TokenType::MnemonicNormal;

    // The groups come from cs_detail and are the architecture-neutral half of the
    // classification: a call is a call on both architectures, whatever it is spelled.
    bool isJump = false;
    bool isCall = false;
    bool isReturn = false;
    if(insn.detail != nullptr)
    {
        for(uint8_t i = 0; i < insn.detail->groups_count; i++)
        {
            switch(insn.detail->groups[i])
            {
            case CS_GRP_JUMP:   isJump = true; break;
            case CS_GRP_CALL:   isCall = true; break;
            case CS_GRP_RET:    isReturn = true; break;
            default: break;
            }
        }
    }

    if(isCall)
        type = TokenType::MnemonicCall;
    else if(isReturn)
        type = TokenType::MnemonicRet;
    else if(isJump)
    {
        // Conditional or not is not a group; it is whether the mnemonic carries a condition.
        // arm64 spells it `b.eq`, x86 spells it `je`/`jne`/`jg`. Anything that is a jump and is
        // not the bare unconditional form is treated as conditional.
        const bool bareJump = mnemonic == QLatin1String("b") || mnemonic == QLatin1String("br") ||
                              mnemonic == QLatin1String("jmp");
        type = bareJump ? TokenType::MnemonicUncondJump : TokenType::MnemonicCondJump;
    }
    else if(mnemonic == QLatin1String("nop"))
        type = TokenType::MnemonicNop;
    else if(mnemonic == QLatin1String("int3") || mnemonic == QLatin1String("brk"))
        type = TokenType::MnemonicInt3;
    else if(mnemonic.startsWith(QLatin1String("push")) || mnemonic.startsWith(QLatin1String("pop")) ||
            mnemonic == QLatin1String("stp") || mnemonic == QLatin1String("ldp"))
    {
        // stp/ldp are arm64's stack pair instructions -- the prologue and epilogue the spec's
        // analysis section names. They are what push/pop mean on this architecture.
        type = TokenType::MnemonicPushPop;
    }

    emitToken(type, mnemonic, out);
    emitToken(TokenType::Space, QStringLiteral(" "), out);
}

void CapstoneTokenizer::emitArm64Operands(const csh handle, const cs_insn& insn,
                                         InstructionToken& out) const
{
    if(insn.detail == nullptr)
        return;

    const cs_arm64& detail = insn.detail->arm64;
    for(uint8_t i = 0; i < detail.op_count; i++)
    {
        if(i > 0)
        {
            emitToken(TokenType::Comma, QStringLiteral(","), out);
            emitToken(TokenType::ArgumentSpace, QStringLiteral(" "), out);
        }

        const cs_arm64_op& op = detail.operands[i];
        switch(op.type)
        {
        case ARM64_OP_REG:
            emitRegister(handle, op.reg, TokenType::Uncategorized, out);
            break;

        case ARM64_OP_IMM:
            emitValue(TokenType::Value, static_cast<duint>(op.imm), sizeof(duint), out);
            break;

        case ARM64_OP_MEM:
            emitToken(TokenType::MemoryBrackets, QStringLiteral("["), out);
            if(op.mem.base != ARM64_REG_INVALID)
                emitRegister(handle, op.mem.base, TokenType::MemoryBaseRegister, out);
            if(op.mem.index != ARM64_REG_INVALID)
            {
                emitToken(TokenType::MemoryOperator, QStringLiteral("+"), out);
                emitRegister(handle, op.mem.index, TokenType::MemoryIndexRegister, out);
            }
            if(op.mem.disp != 0)
            {
                emitToken(TokenType::MemoryOperator, op.mem.disp < 0 ? QStringLiteral("-")
                                                                : QStringLiteral("+"), out);
                emitValue(TokenType::Address,
                          static_cast<duint>(op.mem.disp < 0 ? -op.mem.disp : op.mem.disp),
                          sizeof(duint), out);
            }
            emitToken(TokenType::MemoryBrackets, QStringLiteral("]"), out);
            break;

        default:
            // Shifts, barriers, system registers and the rest. Marked rather than guessed at.
            emitToken(TokenType::Uncategorized, QString::fromUtf8(insn.op_str), out);
            return;
        }
    }
}

void CapstoneTokenizer::emitX86Operands(const csh handle, const cs_insn& insn,
                                       InstructionToken& out) const
{
    if(insn.detail == nullptr)
        return;

    const cs_x86& detail = insn.detail->x86;
    for(uint8_t i = 0; i < detail.op_count; i++)
    {
        if(i > 0)
        {
            emitToken(TokenType::Comma, QStringLiteral(","), out);
            emitToken(TokenType::ArgumentSpace, QStringLiteral(" "), out);
        }

        const cs_x86_op& op = detail.operands[i];
        switch(op.type)
        {
        case X86_OP_REG:
            emitRegister(handle, op.reg, TokenType::Uncategorized, out);
            break;

        case X86_OP_IMM:
            emitValue(TokenType::Value, static_cast<duint>(op.imm), op.size, out);
            break;

        case X86_OP_MEM:
            if(op.mem.segment != X86_REG_INVALID)
            {
                emitRegister(handle, op.mem.segment, TokenType::MemorySegment, out);
                emitToken(TokenType::MemoryOperator, QStringLiteral(":"), out);
            }
            emitToken(TokenType::MemoryBrackets, QStringLiteral("["), out);
            if(op.mem.base != X86_REG_INVALID)
                emitRegister(handle, op.mem.base, TokenType::MemoryBaseRegister, out);
            if(op.mem.index != X86_REG_INVALID)
            {
                emitToken(TokenType::MemoryOperator, QStringLiteral("+"), out);
                emitRegister(handle, op.mem.index, TokenType::MemoryIndexRegister, out);
                if(op.mem.scale > 1)
                {
                    emitToken(TokenType::MemoryOperator, QStringLiteral("*"), out);
                    emitToken(TokenType::MemoryScale, QString::number(op.mem.scale), out);
                }
            }
            if(op.mem.disp != 0)
            {
                emitToken(TokenType::MemoryOperator, op.mem.disp < 0 ? QStringLiteral("-")
                                                                : QStringLiteral("+"), out);
                emitValue(TokenType::Address,
                          static_cast<duint>(op.mem.disp < 0 ? -op.mem.disp : op.mem.disp),
                          sizeof(duint), out);
            }
            emitToken(TokenType::MemoryBrackets, QStringLiteral("]"), out);
            break;

        default:
            emitToken(TokenType::Uncategorized, QString::fromUtf8(insn.op_str), out);
            return;
        }
    }
}

bool CapstoneTokenizer::TokenizeInstruction(const csh handle, const cs_insn& insn,
                                            InstructionToken& instruction) const
{
    instruction.tokens.clear();
    emitMnemonic(insn, instruction);

    if(mArchitecture != nullptr && mArchitecture->cpu() == Architecture::Cpu::Arm64)
        emitArm64Operands(handle, insn, instruction);
    else
        emitX86Operands(handle, insn, instruction);

    return true;
}
