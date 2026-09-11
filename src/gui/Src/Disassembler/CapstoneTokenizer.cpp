#include "CapstoneTokenizer.h"
#include "Configuration.h"
#include "StringUtil.h"
#include <Utils/CachedFontMetrics.h>
#include <Bridge.h>

#include <capstone/arm64.h>
#include <capstone/x86.h>

CapstoneTokenizer::CapstoneTokenizer(int maxModuleLength, Architecture* architecture)
    : mMaxModuleLength(maxModuleLength),
      mArchitecture(architecture)
{
}

static CapstoneTokenizer::TokenColor colorNamesMap[size_t(CapstoneTokenizer::TokenType::Last)];
QHash<QString, int> CapstoneTokenizer::gStringPool;
int CapstoneTokenizer::gPoolId = 0;

void CapstoneTokenizer::addColorName(TokenType type, QString color, QString backgroundColor)
{
    colorNamesMap[int(type)] = TokenColor(color, backgroundColor);
}

CapstoneTokenizer::TokenColor CapstoneTokenizer::getTokenColor(TokenType type)
{
    return colorNamesMap[(size_t)type];
}

void CapstoneTokenizer::addStringsToPool(const QString & strings)
{
    QStringList stringList = strings.split(' ', Qt::SkipEmptyParts);
    for(const QString & string : stringList)
        gStringPool.insert(string, gPoolId);
    gPoolId++;
}

void CapstoneTokenizer::UpdateColors()
{
    //filling
    addColorName(TokenType::Comma, "InstructionCommaColor", "InstructionCommaBackgroundColor");
    addColorName(TokenType::Space, "", "");
    addColorName(TokenType::ArgumentSpace, "", "");
    addColorName(TokenType::MemoryOperatorSpace, "", "");
    //general instruction parts
    addColorName(TokenType::Prefix, "InstructionPrefixColor", "InstructionPrefixBackgroundColor");
    addColorName(TokenType::Uncategorized, "InstructionUncategorizedColor", "InstructionUncategorizedBackgroundColor");
    addColorName(TokenType::Address, "InstructionAddressColor", "InstructionAddressBackgroundColor"); //jump/call destinations
    addColorName(TokenType::Value, "InstructionValueColor", "InstructionValueBackgroundColor");
    addColorName(TokenType::TraceNewValue, "TraceNewValueColor", "TraceNewValueBackgroundColor");
    //mnemonics
    addColorName(TokenType::MnemonicNormal, "InstructionMnemonicColor", "InstructionMnemonicBackgroundColor");
    addColorName(TokenType::MnemonicPushPop, "InstructionPushPopColor", "InstructionPushPopBackgroundColor");
    addColorName(TokenType::MnemonicCall, "InstructionCallColor", "InstructionCallBackgroundColor");
    addColorName(TokenType::MnemonicRet, "InstructionRetColor", "InstructionRetBackgroundColor");
    addColorName(TokenType::MnemonicCondJump, "InstructionConditionalJumpColor", "InstructionConditionalJumpBackgroundColor");
    addColorName(TokenType::MnemonicUncondJump, "InstructionUnconditionalJumpColor", "InstructionUnconditionalJumpBackgroundColor");
    addColorName(TokenType::MnemonicNop, "InstructionNopColor", "InstructionNopBackgroundColor");
    addColorName(TokenType::MnemonicFar, "InstructionFarColor", "InstructionFarBackgroundColor");
    addColorName(TokenType::MnemonicInt3, "InstructionInt3Color", "InstructionInt3BackgroundColor");
    addColorName(TokenType::MnemonicUnusual, "InstructionUnusualColor", "InstructionUnusualBackgroundColor");
    //memory
    addColorName(TokenType::MemorySize, "InstructionMemorySizeColor", "InstructionMemorySizeBackgroundColor");
    addColorName(TokenType::MemorySegment, "InstructionMemorySegmentColor", "InstructionMemorySegmentBackgroundColor");
    addColorName(TokenType::MemoryBrackets, "InstructionMemoryBracketsColor", "InstructionMemoryBracketsBackgroundColor");
    addColorName(TokenType::MemoryStackBrackets, "InstructionMemoryStackBracketsColor", "InstructionMemoryStackBracketsBackgroundColor");
    addColorName(TokenType::MemoryBaseRegister, "InstructionMemoryBaseRegisterColor", "InstructionMemoryBaseRegisterBackgroundColor");
    addColorName(TokenType::MemoryIndexRegister, "InstructionMemoryIndexRegisterColor", "InstructionMemoryIndexRegisterBackgroundColor");
    addColorName(TokenType::MemoryScale, "InstructionMemoryScaleColor", "InstructionMemoryScaleBackgroundColor");
    addColorName(TokenType::MemoryOperator, "InstructionMemoryOperatorColor", "InstructionMemoryOperatorBackgroundColor");
    //registers
    addColorName(TokenType::GeneralRegister, "InstructionGeneralRegisterColor", "InstructionGeneralRegisterBackgroundColor");
    addColorName(TokenType::FpuRegister, "InstructionFpuRegisterColor", "InstructionFpuRegisterBackgroundColor");
    addColorName(TokenType::MmxRegister, "InstructionMmxRegisterColor", "InstructionMmxRegisterBackgroundColor");
    addColorName(TokenType::XmmRegister, "InstructionXmmRegisterColor", "InstructionXmmRegisterBackgroundColor");
    addColorName(TokenType::YmmRegister, "InstructionYmmRegisterColor", "InstructionYmmRegisterBackgroundColor");
    addColorName(TokenType::ZmmRegister, "InstructionZmmRegisterColor", "InstructionZmmRegisterBackgroundColor");
}

void CapstoneTokenizer::UpdateStringPool()
{
    gPoolId = 0;
    gStringPool.clear();
    // These registers must be in lower case.
    addStringsToPool("rax eax ax al ah");
    addStringsToPool("rbx ebx bx bl bh");
    addStringsToPool("rcx ecx cx cl ch");
    addStringsToPool("rdx edx dx dl dh");
    addStringsToPool("rsi esi si sil");
    addStringsToPool("rdi edi di dil");
    addStringsToPool("rbp ebp bp bpl");
    addStringsToPool("rsp esp sp spl");
    addStringsToPool("r8 r8d r8w r8b");
    addStringsToPool("r9 r9d r9w r9b");
    addStringsToPool("r10 r10d r10w r10b");
    addStringsToPool("r11 r11d r11w r11b");
    addStringsToPool("r12 r12d r12w r12b");
    addStringsToPool("r13 r13d r13w r13b");
    addStringsToPool("r14 r14d r14w r14b");
    addStringsToPool("r15 r15d r15w r15b");
    addStringsToPool("xmm0 ymm0 zmm0");
    addStringsToPool("xmm1 ymm1 zmm1");
    addStringsToPool("xmm2 ymm2 zmm2");
    addStringsToPool("xmm3 ymm3 zmm3");
    addStringsToPool("xmm4 ymm4 zmm4");
    addStringsToPool("xmm5 ymm5 zmm5");
    addStringsToPool("xmm6 ymm6 zmm6");
    addStringsToPool("xmm7 ymm7 zmm7");
    addStringsToPool("xmm8 ymm8 zmm8");
    addStringsToPool("xmm9 ymm9 zmm9");
    addStringsToPool("xmm10 ymm10 zmm10");
    addStringsToPool("xmm11 ymm11 zmm11");
    addStringsToPool("xmm12 ymm12 zmm12");
    addStringsToPool("xmm13 ymm13 zmm13");
    addStringsToPool("xmm14 ymm14 zmm14");
    addStringsToPool("xmm15 ymm15 zmm15");
    addStringsToPool("xmm16 ymm16 zmm16");
    addStringsToPool("xmm17 ymm17 zmm17");
    addStringsToPool("xmm18 ymm18 zmm18");
    addStringsToPool("xmm19 ymm19 zmm19");
    addStringsToPool("xmm20 ymm20 zmm20");
    addStringsToPool("xmm21 ymm21 zmm21");
    addStringsToPool("xmm22 ymm22 zmm22");
    addStringsToPool("xmm23 ymm23 zmm23");
    addStringsToPool("xmm24 ymm24 zmm24");
    addStringsToPool("xmm25 ymm25 zmm25");
    addStringsToPool("xmm26 ymm26 zmm26");
    addStringsToPool("xmm27 ymm27 zmm27");
    addStringsToPool("xmm28 ymm28 zmm28");
    addStringsToPool("xmm29 ymm29 zmm29");
    addStringsToPool("xmm30 ymm30 zmm30");
    addStringsToPool("xmm31 ymm31 zmm31");
}


bool CapstoneTokenizer::TokenizeData(const QString & datatype, const QString & data, InstructionToken & instruction)
{
    mInst = InstructionToken();
    mIsNop = false;

    tokenizeMnemonic(TokenType::MnemonicNormal, datatype);

    addToken(TokenType::Value, data);

    instruction = mInst;

    return true;
}

void CapstoneTokenizer::TokenizeTraceRegister(const char* reg, duint oldValue, duint newValue, std::vector<SingleToken> & tokens)
{
    if(tokens.size() > 0)
    {
        tokens.push_back(SingleToken(TokenType::ArgumentSpace, " ", TokenValue()));
    }
    QString regName(reg);
    tokens.push_back(SingleToken(TokenType::GeneralRegister, ConfigBool("Disassembler", "Uppercase") ? regName.toUpper() : regName, TokenValue()));
    tokens.push_back(SingleToken(TokenType::ArgumentSpace, ": ", TokenValue()));
    tokens.push_back(SingleToken(TokenType::Value, ToHexString(oldValue), TokenValue(8, oldValue)));
    tokens.push_back(SingleToken(TokenType::ArgumentSpace, QString::fromUtf8("\xe2\x86\x92"), TokenValue())); // Right Arrow
    tokens.push_back(SingleToken(TokenType::TraceNewValue, ToHexString(newValue), TokenValue(8, newValue)));
}

void CapstoneTokenizer::TokenizeTraceMemory(duint address, duint oldValue, duint newValue, std::vector<SingleToken> & tokens)
{
    if(tokens.size() > 0)
    {
        tokens.push_back(SingleToken(TokenType::ArgumentSpace, " ", TokenValue()));
    }
    tokens.push_back(SingleToken(TokenType::Address, ToPtrString(address), TokenValue(8, address)));
    tokens.push_back(SingleToken(TokenType::ArgumentSpace, ": ", TokenValue()));
    tokens.push_back(SingleToken(TokenType::Value, ToHexString(oldValue), TokenValue(8, oldValue)));
    tokens.push_back(SingleToken(TokenType::ArgumentSpace, QString::fromUtf8("\xe2\x86\x92"), TokenValue())); // Right Arrow
    tokens.push_back(SingleToken(TokenType::TraceNewValue, ToHexString(newValue), TokenValue(8, newValue)));
}

void CapstoneTokenizer::UpdateConfig()
{
    mUppercase = ConfigBool("Disassembler", "Uppercase");
    mTabbedMnemonic = ConfigBool("Disassembler", "TabbedMnemonic");
    mArgumentSpaces = ConfigBool("Disassembler", "ArgumentSpaces");
    mHidePointerSizes = ConfigBool("Disassembler", "HidePointerSizes");
    mHideNormalSegments = ConfigBool("Disassembler", "HideNormalSegments");
    mMemorySpaces = ConfigBool("Disassembler", "MemorySpaces");
    mNoHighlightOperands = ConfigBool("Disassembler", "NoHighlightOperands");
    mNoCurrentModuleText = ConfigBool("Disassembler", "NoCurrentModuleText");
    mValueNotation = (DisasmValueNotationType)ConfigUint("Disassembler", "0xPrefixValues");
    mMaxModuleLength = (int)ConfigUint("Disassembler", "MaxModuleSize");
    UpdateStringPool();
}


void CapstoneTokenizer::SetConfig(bool bUppercase, bool bTabbedMnemonic, bool bArgumentSpaces, bool bHidePointerSizes, bool bHideNormalSegments, bool bMemorySpaces, bool bNoHighlightOperands, bool bNoCurrentModuleText, DisasmValueNotationType ValueNotation)
{
    mUppercase = bUppercase;
    mTabbedMnemonic = bTabbedMnemonic;
    mArgumentSpaces = bArgumentSpaces;
    mHidePointerSizes = bHidePointerSizes;
    mHideNormalSegments = bHideNormalSegments;
    mMemorySpaces = bMemorySpaces;
    mNoHighlightOperands = bNoHighlightOperands;
    mNoCurrentModuleText = bNoCurrentModuleText;
    mValueNotation = ValueNotation;
}



void CapstoneTokenizer::TokenToRichText(const InstructionToken & instr, RichTextPainter::List & richTextList, const SingleToken* highlightToken)
{
    QColor highlightColor = ConfigColor("InstructionHighlightColor");
    QColor highlightBackgroundColor = ConfigColor("InstructionHighlightBackgroundColor");
    for(const auto & token : instr.tokens)
    {
        RichTextPainter::CustomRichText_t richText;
        richText.flags = RichTextPainter::FlagNone;
        richText.text = token.text;
        richText.underline = false;
        if(token.type < TokenType::Last)
        {
            const auto & tokenColor = colorNamesMap[int(token.type)];
            richText.flags = tokenColor.flags;
            if(TokenEquals(&token, highlightToken))
            {
                richText.textColor = highlightColor;
                richText.textBackground = highlightBackgroundColor;
            }
            else
            {
                richText.textColor = tokenColor.color;
                richText.textBackground = tokenColor.backgroundColor;
            }
        }
        richTextList.push_back(richText);
    }
}

bool CapstoneTokenizer::TokenFromX(const InstructionToken & instr, SingleToken & token, int x, CachedFontMetrics* fontMetrics)
{
    if(x < instr.x) //before the first token
        return false;
    int len = int(instr.tokens.size());
    for(int i = 0, xStart = instr.x; i < len; i++)
    {
        const auto & curToken = instr.tokens.at(i);
        int curWidth = fontMetrics->width(curToken.text);
        int xEnd = xStart + curWidth;
        if(x >= xStart && x < xEnd)
        {
            token = curToken;
            return true;
        }
        xStart = xEnd;
    }
    return false; //not found
}

bool CapstoneTokenizer::IsHighlightableToken(const SingleToken & token)
{
    switch(token.type)
    {
    case TokenType::Comma:
    case TokenType::Space:
    case TokenType::ArgumentSpace:
    case TokenType::Uncategorized:
    case TokenType::MemoryOperatorSpace:
    case TokenType::MemoryBrackets:
    case TokenType::MemoryStackBrackets:
    case TokenType::MemoryOperator:
        return false;
        break;
    default:
        return true;
    }
}

bool CapstoneTokenizer::tokenTextPoolEquals(const QString & a, const QString & b)
{
    if(a.compare(b, Qt::CaseInsensitive) == 0)
        return true;
    auto found1 = gStringPool.find(a.toLower());
    auto found2 = gStringPool.find(b.toLower());
    if(found1 == gStringPool.end() || found2 == gStringPool.end())
        return false;
    return found1.value() == found2.value();
}

bool CapstoneTokenizer::TokenEquals(const SingleToken* a, const SingleToken* b, bool ignoreSize)
{
    if(!a || !b)
        return false;
    if(a->value.size != 0 && b->value.size != 0) //we have a value
    {
        if(!ignoreSize && a->value.size != b->value.size)
            return false;
        else if(a->value.value != b->value.value)
            return false;
    }
    return tokenTextPoolEquals(a->text, b->text);
}


static bool tokenIsSpace(CapstoneTokenizer::TokenType type)
{
    return type == CapstoneTokenizer::TokenType::Space || type == CapstoneTokenizer::TokenType::ArgumentSpace || type == CapstoneTokenizer::TokenType::MemoryOperatorSpace;
}

void CapstoneTokenizer::addToken(TokenType type, QString text, const TokenValue & value)
{
    bool isItSpaceType = tokenIsSpace(type);
    if(!isItSpaceType)
    {
        text = text.trimmed();
    }

    if(mUppercase && !value.size)
        text = text.toUpper();

    if(isItSpaceType)
    {
        mInst.tokens.push_back(SingleToken(type, text, value));
    }
    else if(type == TokenType::Address && text.startsWith("<&"))
    {
        mInst.tokens.push_back(SingleToken(TokenType::Address, "<", value));
        mInst.tokens.push_back(SingleToken(TokenType::MemoryBaseRegister, "&", value));
        mInst.tokens.push_back(SingleToken(TokenType::Address, text.mid(2)));
    }
    else
    {
        mInst.tokens.push_back(SingleToken(mIsNop ? TokenType::MnemonicNop : type, text, value));
    }
}

void CapstoneTokenizer::addToken(TokenType type, const QString & text)
{
    addToken(type, text, TokenValue());
}

void CapstoneTokenizer::addMemoryOperator(char operatorText)
{
    if(mMemorySpaces)
        addToken(TokenType::MemoryOperatorSpace, " ");
    QString text;
    text += operatorText;
    addToken(TokenType::MemoryOperator, text);
    if(mMemorySpaces)
        addToken(TokenType::MemoryOperatorSpace, " ");
}

QString CapstoneTokenizer::printValue(const TokenValue & value, bool expandModule) const
{
    QString labelText;
    char label[MAX_LABEL_SIZE] = "";
    char module[MAX_MODULE_SIZE] = "";
    QString moduleText;
    duint addr = value.value;
    bool bHasLabel = DbgGetLabelAt(addr, SEG_DEFAULT, label);
    labelText = QString(label);
    bool bHasModule;
    if(mNoCurrentModuleText)
    {
        duint size, base;
        // The instruction's own address, which used to be asked of the decoder. It is
        // recorded when tokenizing starts instead: the tokenizer no longer owns a decoder
        // to ask, and the answer was never the decoder's to give -- it is where this
        // instruction is, which the caller already knew.
        base = DbgMemFindBaseAddr(mCurrentAddress, &size);
        if(addr >= base && addr < base + size)
            bHasModule = false;
        else
            bHasModule = (expandModule && DbgGetModuleAt(addr, module) && !QString(labelText).startsWith("JMP.&"));
    }
    else
        bHasModule = (expandModule && DbgGetModuleAt(addr, module) && !QString(labelText).startsWith("JMP.&"));
    moduleText = QString(module);
    if(mMaxModuleLength != -1)
        moduleText.truncate(mMaxModuleLength);
    if(moduleText.length())
        moduleText += ".";
    QString addrText = ToHexString(addr);
    QString finalText;
    if(bHasLabel && bHasModule)  //<module.label>
        finalText = QString("<%1%2>").arg(moduleText).arg(labelText);
    else if(bHasModule)  //module.addr
        finalText = QString("%1%2").arg(moduleText).arg(addrText);
    else if(bHasLabel)  //<label>
        finalText = QString("<%1>").arg(labelText);
    else
    {
        switch(mValueNotation)
        {
        case DisasmValueNotationC:
            finalText = "0x" + addrText;
            break;
        case DisasmValueNotationMASM:
            if((addrText[0] >= 'A' && addrText[0] <= 'F') || (addrText[0] >= 'a' && addrText[0] <= 'f'))
                finalText = "0" + addrText + "h";
            else
                finalText = addrText + "h";
            break;
        default:
            finalText = addrText;
            break;
        }
    }

    return finalText;
}

bool CapstoneTokenizer::TokenizeInstruction(const csh handle, const cs_insn & insn,
                                            InstructionToken & instruction)
{
    mInst = InstructionToken();
    mCurrentAddress = static_cast<duint>(insn.address);

    tokenizeMnemonic(insn);

    if(mArchitecture != nullptr && mArchitecture->cpu() == Architecture::Cpu::Arm64)
        tokenizeArm64Operands(handle, insn);
    else
        tokenizeX86Operands(handle, insn);

    instruction = mInst;
    return true;
}

void CapstoneTokenizer::tokenizeMnemonic(const cs_insn & insn)
{
    const QString mnemonic = QString::fromUtf8(insn.mnemonic);
    TokenType type = TokenType::MnemonicNormal;

    bool isJump = false;
    bool isCall = false;
    bool isReturn = false;
    if(insn.detail != nullptr)
    {
        for(uint8_t i = 0; i < insn.detail->groups_count; i++)
        {
            switch(insn.detail->groups[i])
            {
            case CS_GRP_JUMP: isJump = true; break;
            case CS_GRP_CALL: isCall = true; break;
            case CS_GRP_RET: isReturn = true; break;
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
        // MEASURED (milestone 6 task 4): the groups do NOT separate conditional from
        // unconditional -- b.eq and b carry identical ones, as do je and jmp. The condition is
        // a field: cs_arm64.cc on arm64, the instruction id on x86-64.
        bool conditional = false;
        if(mArchitecture != nullptr && mArchitecture->cpu() == Architecture::Cpu::Arm64)
        {
            const arm64_cc cc = insn.detail != nullptr ? insn.detail->arm64.cc : ARM64_CC_INVALID;
            conditional = cc != ARM64_CC_INVALID && cc != ARM64_CC_AL && cc != ARM64_CC_NV;
        }
        else
        {
            conditional = insn.id != X86_INS_JMP;
        }
        type = conditional ? TokenType::MnemonicCondJump : TokenType::MnemonicUncondJump;
    }
    else if(mnemonic == QLatin1String("nop"))
        type = TokenType::MnemonicNop;
    else if(mnemonic == QLatin1String("int3") || mnemonic == QLatin1String("brk"))
        type = TokenType::MnemonicInt3;
    else if(mnemonic.startsWith(QLatin1String("push")) || mnemonic.startsWith(QLatin1String("pop")) ||
            mnemonic == QLatin1String("stp") || mnemonic == QLatin1String("ldp"))
    {
        // stp and ldp are arm64's stack pair instructions -- the prologue and epilogue the
        // spec's analysis section names. They are what push and pop mean here.
        type = TokenType::MnemonicPushPop;
    }

    tokenizeMnemonic(type, mnemonic);
}

void CapstoneTokenizer::tokenizeMnemonic(TokenType type, const QString & mnemonic)
{
    addToken(type, mnemonic);
    if(mTabbedMnemonic)
    {
        int spaces = 7 - mnemonic.length();
        if(spaces < 1)
            spaces = 1;
        addToken(TokenType::Space, QString(spaces, QChar(' ')));
    }
    else
    {
        addToken(TokenType::Space, " ");
    }
}

CapstoneTokenizer::TokenType CapstoneTokenizer::registerTypeOf(const QString & name)
{
    if(name.isEmpty())
        return TokenType::Uncategorized;

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

    // arm64's v/q/d/s/h registers are the SIMD and floating-point file, mapped onto the Xmm
    // token type rather than given one of their own: the type list is x86's, extending it is
    // not this milestone's job, and a SIMD register coloured as a SIMD register is the property
    // a reader actually wants.
    const QChar first = name.at(0).toLower();
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

void CapstoneTokenizer::tokenizeRegister(const csh handle, const unsigned reg, const TokenType type)
{
    const char* const name = cs_reg_name(handle, reg);
    if(name == nullptr)
    {
        addToken(TokenType::Uncategorized, "?");
        return;
    }

    const QString text = QString::fromUtf8(name);
    addToken(type == TokenType::Uncategorized ? registerTypeOf(text) : type,
             mUppercase ? text.toUpper() : text);
}

void CapstoneTokenizer::tokenizeArm64Operands(const csh handle, const cs_insn & insn)
{
    if(insn.detail == nullptr)
        return;

    const cs_arm64 & detail = insn.detail->arm64;
    for(uint8_t i = 0; i < detail.op_count; i++)
    {
        if(i > 0)
        {
            addToken(TokenType::Comma, ",");
            if(mArgumentSpaces)
                addToken(TokenType::ArgumentSpace, " ");
        }

        const cs_arm64_op & op = detail.operands[i];
        switch(op.type)
        {
        case ARM64_OP_REG:
            tokenizeRegister(handle, op.reg, TokenType::Uncategorized);
            break;

        case ARM64_OP_IMM:
            addToken(TokenType::Value, printValue(TokenValue(sizeof(duint), static_cast<duint>(op.imm)), false));
            break;

        case ARM64_OP_MEM:
            addToken(TokenType::MemoryBrackets, "[");
            if(op.mem.base != ARM64_REG_INVALID)
                tokenizeRegister(handle, op.mem.base, TokenType::MemoryBaseRegister);
            if(op.mem.index != ARM64_REG_INVALID)
            {
                addMemoryOperator('+');
                tokenizeRegister(handle, op.mem.index, TokenType::MemoryIndexRegister);
            }
            if(op.mem.disp != 0)
            {
                addMemoryOperator(op.mem.disp < 0 ? '-' : '+');
                const duint magnitude = static_cast<duint>(op.mem.disp < 0 ? -op.mem.disp
                                                                           : op.mem.disp);
                const TokenValue value(sizeof(duint), magnitude);
                addToken(TokenType::Address, printValue(value, true), value);
            }
            addToken(TokenType::MemoryBrackets, "]");
            break;

        default:
            // Shifts, barriers, system registers and the rest: marked rather than guessed at. A
            // token that says "I do not know what this is" costs a colour; one that guesses
            // wrong costs trust in every other colour on the line.
            addToken(TokenType::Uncategorized, QString::fromUtf8(insn.op_str));
            return;
        }
    }
}

void CapstoneTokenizer::tokenizeX86Operands(const csh handle, const cs_insn & insn)
{
    if(insn.detail == nullptr)
        return;

    const cs_x86 & detail = insn.detail->x86;
    for(uint8_t i = 0; i < detail.op_count; i++)
    {
        if(i > 0)
        {
            addToken(TokenType::Comma, ",");
            if(mArgumentSpaces)
                addToken(TokenType::ArgumentSpace, " ");
        }

        const cs_x86_op & op = detail.operands[i];
        switch(op.type)
        {
        case X86_OP_REG:
            tokenizeRegister(handle, op.reg, TokenType::Uncategorized);
            break;

        case X86_OP_IMM:
        {
            const TokenValue value(op.size, static_cast<duint>(op.imm));
            addToken(TokenType::Value, printValue(value, false), value);
            break;
        }

        case X86_OP_MEM:
            if(op.mem.segment != X86_REG_INVALID)
            {
                tokenizeRegister(handle, op.mem.segment, TokenType::MemorySegment);
                addToken(TokenType::Uncategorized, ":");
            }
            addToken(TokenType::MemoryBrackets, "[");
            if(op.mem.base != X86_REG_INVALID)
                tokenizeRegister(handle, op.mem.base, TokenType::MemoryBaseRegister);
            if(op.mem.index != X86_REG_INVALID)
            {
                addMemoryOperator('+');
                tokenizeRegister(handle, op.mem.index, TokenType::MemoryIndexRegister);
                if(op.mem.scale > 1)
                {
                    addMemoryOperator('*');
                    addToken(TokenType::MemoryScale, QString::number(op.mem.scale));
                }
            }
            if(op.mem.disp != 0)
            {
                addMemoryOperator(op.mem.disp < 0 ? '-' : '+');
                const duint magnitude = static_cast<duint>(op.mem.disp < 0 ? -op.mem.disp
                                                                           : op.mem.disp);
                const TokenValue value(sizeof(duint), magnitude);
                addToken(TokenType::Address, printValue(value, true), value);
            }
            addToken(TokenType::MemoryBrackets, "]");
            break;

        default:
            addToken(TokenType::Uncategorized, QString::fromUtf8(insn.op_str));
            return;
        }
    }
}
