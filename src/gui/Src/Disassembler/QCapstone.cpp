#include "QCapstone.h"

#include "CapstoneVersion.h"
#include "StringUtil.h"

#include <algorithm>
#include <Utils/EncodeMap.h>
#include <Utils/CodeFolding.h>
#include <Bridge.h>

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
{
    Q_UNUSED(maxModuleSize);

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
