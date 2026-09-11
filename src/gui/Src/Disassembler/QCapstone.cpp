#include "QCapstone.h"

#include "CapstoneVersion.h"
#include "StringUtil.h"

#include <algorithm>
#include <Utils/EncodeMap.h>
#include <Utils/CodeFolding.h>
#include <Bridge.h>

#include <capstone/arm64.h>
#include <capstone/x86.h>

namespace
{
    // What a failed decode costs the walk. QZydis uses 2 with the reasoning in a comment: FF FE
    // and FE FF are usually part of an instruction rather than two one-byte ones, so advancing
    // by two lands the backwards walk in better places on real code. Kept because the view's
    // behaviour depends on where that walk lands, not only on it terminating.
    constexpr uint kUndecodableStride = 2;

    // The longest instruction either architecture has: 15 bytes on x86-64, 4 on arm64. The walk
    // backwards needs a window big enough to contain n of them.
    constexpr uint kMaxInstructionLength = 16;
}

QCapstone::QCapstone(int maxModuleSize, Architecture* architecture)
    : mArchitecture(architecture)
    , mTokenizer(maxModuleSize, architecture)
{

    // Both handles, once. CS_OPT_DETAIL is what the token stream is built from (spec section 7);
    // without it Capstone returns text and nothing a highlighter can classify.
    if(cs_open(MACHDBG_CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &mArm64Handle) == CS_ERR_OK)
    {
        cs_option(mArm64Handle, CS_OPT_DETAIL, CS_OPT_ON);
        mScratch = cs_malloc(mArm64Handle);
    }
    if(cs_open(CS_ARCH_X86, CS_MODE_64, &mX86Handle) == CS_ERR_OK)
    {
        cs_option(mX86Handle, CS_OPT_DETAIL, CS_OPT_ON);
        if(mScratch == nullptr)
            mScratch = cs_malloc(mX86Handle);
    }

    UpdateDataInstructionMap();
    mEncodeMap = new EncodeMap();
}

QCapstone::~QCapstone()
{
    if(mScratch != nullptr)
        cs_free(mScratch, 1);
    if(mArm64Handle != 0)
        cs_close(&mArm64Handle);
    if(mX86Handle != 0)
        cs_close(&mX86Handle);
    delete mEncodeMap;
}

csh QCapstone::handle() const
{
    // Architecture::cpu() is what says which. It defaults to X86 so that the three x86-only
    // implementations of that interface (hex_viewer, minidump, the Linux debugger) keep working
    // untouched; regview overrides it from the engine's own DbgArch.
    return mArchitecture != nullptr && mArchitecture->cpu() == Architecture::Cpu::Arm64
           ? mArm64Handle
           : mX86Handle;
}

bool QCapstone::decodeOne(const uint8_t* data, const duint size, const duint address,
                          cs_insn* insn) const
{
    const csh h = handle();
    if(h == 0 || data == nullptr || size == 0 || insn == nullptr)
        return false;

    const uint8_t* cursor = data;
    size_t remaining = size;
    uint64_t at = address;
    return cs_disasm_iter(h, &cursor, &remaining, &at, insn);
}

Instruction_t QCapstone::DisassembleAt(const uint8_t* data, const duint size,
                                       const duint origBase, const duint origInstRVA,
                                       const bool datainstr)
{
    if(datainstr)
    {
        const ENCODETYPE type = mEncodeMap->getDataType(origBase + origInstRVA);
        if(!mEncodeMap->isCode(type))
            return DecodeDataAt(data, size, origBase, origInstRVA, type);
    }

    Instruction_t inst;
    inst.rva = origInstRVA;

    const bool decoded = decodeOne(data, size, origBase + origInstRVA, mScratch);
    if(decoded)
    {
        inst.length = static_cast<int>(mScratch->size);
        inst.instStr = QString("%1 %2").arg(mScratch->mnemonic, mScratch->op_str).trimmed();
        mTokenizer.TokenizeInstruction(handle(), *mScratch, inst.tokens);
        fillRegistersReferenced(*mScratch, inst);
        fillBranchInfo(*mScratch, inst);
    }
    else
    {
        // A length of zero would make every walk over this instruction loop forever, and a
        // debugger reads undecodable bytes constantly -- they are what data looks like to a
        // disassembler. One byte, shown as a byte.
        inst.length = 1;
        inst.instStr = QStringLiteral("???");
    }

    if(mCodeFoldingManager && mCodeFoldingManager->isFolded(origInstRVA))
    {
        inst.length = static_cast<int>(
            mCodeFoldingManager->getFoldEnd(origInstRVA + origBase) - (origInstRVA + origBase) + 1);
    }

    const int dumpLength = std::min<int>(inst.length, static_cast<int>(size));
    inst.dump = QByteArray(reinterpret_cast<const char*>(data), dumpLength);

    // prefixSize, opcodeSize, group1Size..group3Size and vectorElementType stay zero.
    //
    // They are Zydis's byte-grouping and vector-element APIs, and Capstone has no equivalent.
    // Nothing depends on them for correctness: formatOpcodeString (QZydis.cpp) uses the group
    // sizes only to insert a ':' and spaces between prefix, opcode and operand bytes in the
    // opcode column, so zeroed they render as one unseparated byte string. That is a contained
    // cosmetic loss on x86-64 and meaningless on arm64, where every instruction is four bytes.
    // Said here rather than left for someone to discover a field nobody fills.

    return inst;
}

void QCapstone::fillBranchInfo(const cs_insn& insn, Instruction_t& inst) const
{
    inst.branchType = Instruction_t::None;
    inst.branchDestination = 0;

    if(insn.detail == nullptr)
        return;

    bool isJump = false;
    bool isCall = false;
    for(uint8_t i = 0; i < insn.detail->groups_count; i++)
    {
        if(insn.detail->groups[i] == CS_GRP_JUMP)
            isJump = true;
        else if(insn.detail->groups[i] == CS_GRP_CALL)
            isCall = true;
    }

    if(!isJump && !isCall)
        return;

    const bool arm64 = mArchitecture != nullptr &&
                       mArchitecture->cpu() == Architecture::Cpu::Arm64;

    // MEASURED, and neither of these follows from the group list:
    //
    //   arm64: b.eq and b carry IDENTICAL groups ([JUMP, BRANCH_RELATIVE]). What separates them
    //          is cs_arm64.cc -- 1 for b.eq, 0 (ARM64_CC_INVALID) for b and br.
    //   x86:   je and jmp also carry identical groups. What separates them is the instruction
    //          id: X86_INS_JMP is the unconditional one.
    //
    // So the condition is read from a field rather than from the mnemonic's spelling, which
    // would have worked until the first instruction someone spells differently.
    bool conditional = false;
    if(arm64)
    {
        const arm64_cc cc = insn.detail->arm64.cc;
        conditional = cc != ARM64_CC_INVALID && cc != ARM64_CC_AL && cc != ARM64_CC_NV;
    }
    else
    {
        conditional = insn.id != X86_INS_JMP;
    }

    if(isCall)
        inst.branchType = Instruction_t::Call;
    else
        inst.branchType = conditional ? Instruction_t::Conditional : Instruction_t::Unconditional;

    // MEASURED, and the reason the destination is NOT read from CS_GRP_BRANCH_RELATIVE: `blr x1`
    // -- an indirect call through a register -- reports that group too. The group says the
    // encoding form, not that a destination is knowable. The operand type is what says it: an
    // immediate operand is an address, a register operand is a value this engine cannot know
    // without running the target.
    if(arm64)
    {
        const cs_arm64& detail = insn.detail->arm64;
        if(detail.op_count > 0 && detail.operands[0].type == ARM64_OP_IMM)
            inst.branchDestination = static_cast<duint>(detail.operands[0].imm);
    }
    else
    {
        const cs_x86& detail = insn.detail->x86;
        if(detail.op_count > 0 && detail.operands[0].type == X86_OP_IMM)
            inst.branchDestination = static_cast<duint>(detail.operands[0].imm);
    }

    // Indirect branches keep a destination of 0. Not a placeholder for something better later:
    // `br xN` and `blr xN` are everywhere in Apple code -- jump tables, stubs, Objective-C
    // dispatch -- and the target is a register value. Reporting a number here would be an
    // answer, which is worse than a gap the spec already documents.
}

void QCapstone::fillRegistersReferenced(const cs_insn& insn, Instruction_t& inst) const
{
    // MEASURED before this was written: cs_regs_access answers on BOTH architectures at this
    // Capstone version, with the sets one would want --
    //
    //     ldr x0, [x1, #8]                       read [x1]      write [x0]
    //     stp x29, x30, [sp, #-0x10]!            read [fp lr sp] write [sp]
    //     mov eax, dword ptr [rbx + rcx*4 + 16]  read [rbx rcx]  write [eax]
    //
    // so there is no need for the fallback the plan allowed for (walking the operands and
    // inferring). If a future version stops answering for one architecture, this returns an
    // empty list rather than a wrong one.
    const csh h = handle();
    if(h == 0)
        return;

    cs_regs read = {};
    cs_regs written = {};
    uint8_t readCount = 0;
    uint8_t writtenCount = 0;
    if(cs_regs_access(h, &insn, read, &readCount, written, &writtenCount) != CS_ERR_OK)
        return;

    // The flag vocabulary is the one Instruction_t already carries from the Zydis
    // implementation: bit 0 read, bit 1 write. Implicit/explicit is not distinguished here --
    // Capstone's access list does not separate them, and inventing the distinction would be
    // worse than omitting it.
    constexpr uint8_t kRead = 1 << 0;
    constexpr uint8_t kWrite = 1 << 1;

    inst.regsReferenced.reserve(static_cast<size_t>(readCount) + writtenCount);
    for(uint8_t i = 0; i < readCount; i++)
    {
        if(const char* const name = cs_reg_name(h, read[i]))
            inst.regsReferenced.emplace_back(name, kRead);
    }
    for(uint8_t i = 0; i < writtenCount; i++)
    {
        if(const char* const name = cs_reg_name(h, written[i]))
            inst.regsReferenced.emplace_back(name, kWrite);
    }
}

Instruction_t QCapstone::DecodeDataAt(const uint8_t* data, const duint size, const duint origBase,
                                      const duint origInstRVA, const ENCODETYPE type)
{
    Instruction_t inst;

    const duint dataSize = mEncodeMap->getDataSize(origBase + origInstRVA, 1);
    const auto info = mDataInstMap.find(type);
    const QString mnemonic = info != mDataInstMap.end()
                             ? (mLongDataInst ? info->longName : info->shortName)
                             : QStringLiteral("db");

    inst.instStr = mnemonic;
    inst.rva = origInstRVA;
    inst.length = static_cast<int>(std::max<duint>(dataSize, 1));
    const int dumpLength = std::min<int>(inst.length, static_cast<int>(size));
    inst.dump = QByteArray(reinterpret_cast<const char*>(data), dumpLength);
    inst.branchType = Instruction_t::None;
    inst.branchDestination = 0;
    return inst;
}

ulong QCapstone::DisassembleBack(const uint8_t* data, const duint base, duint size,
                                 duint ip, int n)
{
    if(data == nullptr)
        return 0;

    if(n < 0)
        n = 0;
    else if(n > 127)
        n = 127;

    if(ip >= size)
        ip = size - 1;

    if(n == 0 || ip < static_cast<duint>(n))
        return ip;

    // Far enough behind that n instructions of the longest possible length fit, then decode
    // forwards and keep every boundary. This is QZydis's shape, and it is the only way on a
    // variable-length architecture: there is no way to step backwards over an x86 instruction.
    uint addresses[128] = {};
    duint back = kMaxInstructionLength * static_cast<duint>(n + 3);
    if(ip < back)
        back = ip;

    duint addr = ip - back;
    if(mCodeFoldingManager && mCodeFoldingManager->isFolded(addr + base))
    {
        const duint foldBegin = mCodeFoldingManager->getFoldBegin(addr + base);
        if(foldBegin >= base && foldBegin < size + base)
            addr = foldBegin - base;
    }

    const uint8_t* cursor = data + addr;
    int i = 0;
    for(; addr < ip; i++)
    {
        addresses[i % 128] = static_cast<uint>(addr);

        duint step = 0;
        if(mCodeFoldingManager && mCodeFoldingManager->isFolded(addr + base))
        {
            const duint foldBegin = mCodeFoldingManager->getFoldBegin(addr + base);
            if(foldBegin >= base)
                addr = foldBegin - base;
            step = mCodeFoldingManager->getFoldEnd(addr + base) - (addr + base) + 1;
        }
        else
        {
            step = decodeOne(cursor, size, addr + base, mScratch) ? mScratch->size
                                                                  : kUndecodableStride;
            // The encode map has the last word: a range the user marked as data is one unit,
            // however it happens to decode.
            step = mEncodeMap->getDataSize(base + addr, step);
        }

        if(step == 0)
            step = 1;   // nothing may advance by nothing; the loop is the whole point

        cursor += step;
        addr += step;
        size -= std::min<duint>(step, size);
    }

    if(i < n)
        return addresses[0];
    return addresses[(i - n) % 128];
}

ulong QCapstone::DisassembleNext(const uint8_t* data, const duint base, duint size,
                                 duint ip, int n)
{
    if(data == nullptr)
        return 0;

    if(n <= 0)
        return ip;
    if(ip >= size)
        ip = size - 1;

    const uint8_t* cursor = data + ip;
    for(int i = 0; i < n && ip < size; i++)
    {
        duint step = 0;
        if(mCodeFoldingManager && mCodeFoldingManager->isFolded(ip + base))
        {
            step = mCodeFoldingManager->getFoldEnd(ip + base) - (ip + base) + 1;
        }
        else
        {
            step = decodeOne(cursor, size - ip, ip + base, mScratch) ? mScratch->size
                                                                    : kUndecodableStride;
            step = mEncodeMap->getDataSize(base + ip, step);
        }

        if(step == 0)
            step = 1;

        cursor += step;
        ip += step;
    }

    return ip;
}

void QCapstone::setCodeFoldingManager(CodeFoldingHelper* CodeFoldingManager)
{
    mCodeFoldingManager = CodeFoldingManager;
}

void QCapstone::UpdateArchitecture()
{
    // Nothing to rebuild: both handles are already open, and handle() reads the architecture
    // every time it is asked. The call exists because the interface has it.
}

void QCapstone::UpdateConfig()
{
    mLongDataInst = ConfigBool("Disassembler", "LongDataInstruction");
    mUseRunTrace = ConfigBool("Engine", "TraceRecordEnabledDuringTrace");
}

void QCapstone::UpdateDataInstructionMap()
{
    mDataInstMap.clear();
    mDataInstMap.insert(enc_byte, {"db", "byte", "int8_t"});
    mDataInstMap.insert(enc_word, {"dw", "word", "int16_t"});
    mDataInstMap.insert(enc_dword, {"dd", "dword", "int32_t"});
    mDataInstMap.insert(enc_fword, {"df", "fword", "int48_t"});
    mDataInstMap.insert(enc_qword, {"dq", "qword", "int64_t"});
    mDataInstMap.insert(enc_tbyte, {"dt", "tbyte", "int80_t"});
    mDataInstMap.insert(enc_oword, {"do", "oword", "int128_t"});
    mDataInstMap.insert(enc_mmword, {"mm", "mmword", "m64"});
    mDataInstMap.insert(enc_xmmword, {"xmm", "xmmword", "m128"});
    mDataInstMap.insert(enc_ymmword, {"ymm", "ymmword", "m256"});
    mDataInstMap.insert(enc_zmmword, {"zmm", "zmmword", "m512"});
    mDataInstMap.insert(enc_real4, {"real4", "real4", "float"});
    mDataInstMap.insert(enc_real8, {"real8", "real8", "double"});
    mDataInstMap.insert(enc_real10, {"real10", "real10", "long double"});
    mDataInstMap.insert(enc_ascii, {"ascii", "ascii", "char"});
    mDataInstMap.insert(enc_unicode, {"unicode", "unicode", "wchar_t"});
}
