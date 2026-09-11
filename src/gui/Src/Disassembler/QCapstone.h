#pragma once

#include <QString>
#include <vector>

#include <capstone/capstone.h>

// Instruction_t and the token types still live in QZydis.h/ZydisTokenizer.h. That is temporary
// and ends at milestone 6 task 5, which retires Zydis and moves the shared struct; until then a
// Capstone-based disassembler and a Zydis-based one have to agree about what an instruction is,
// and the way to guarantee that is for both to use the same declaration rather than a copy.
#include "QZydis.h"

class Architecture;
class EncodeMap;
class CodeFoldingHelper;

// The disassembler, on Capstone. It honours the surface QZydis publishes -- the view
// (BasicView/Disassembly.cpp) is written against these calls and their edge behaviour, not
// against an engine -- so that retiring Zydis is a change of link line rather than a rewrite of
// the consumer.
//
// One handle per architecture, opened once. Capstone's own documentation is explicit that
// cs_open is expensive and cs_disasm is not, which is why nothing here opens a handle per
// instruction.
class QCapstone
{
public:
    QCapstone(int maxModuleSize, Architecture* architecture);
    QCapstone(const QCapstone &) = delete;
    QCapstone(QCapstone &&) = delete;
    ~QCapstone();

    // Walks instruction boundaries. On x86-64 neither direction can be done by arithmetic: the
    // instructions are variable length, so going back means decoding forward from a point far
    // enough behind and keeping the boundaries that land. Both mirror QZydis's versions
    // deliberately, including the heuristics -- see the implementation.
    ulong DisassembleBack(const uint8_t* data, duint base, duint size, duint ip, int n);
    ulong DisassembleNext(const uint8_t* data, duint base, duint size, duint ip, int n);

    Instruction_t DisassembleAt(const uint8_t* data, duint size, duint origBase,
                                duint origInstRVA, bool datainstr = true);
    Instruction_t DecodeDataAt(const uint8_t* data, duint size, duint origBase,
                               duint origInstRVA, ENCODETYPE type);

    void setCodeFoldingManager(CodeFoldingHelper* CodeFoldingManager);
    void UpdateConfig();
    void UpdateArchitecture();

    EncodeMap* getEncodeMap()
    {
        return mEncodeMap;
    }

private:
    struct DataInstructionInfo
    {
        QString shortName;
        QString longName;
        QString cName;
    };

    void UpdateDataInstructionMap();

    // The handle for whatever architecture mArchitecture currently names, or 0 if none opened.
    csh handle() const;

    // Decodes one instruction into `insn`, returning false when Capstone will not vouch for the
    // bytes. A caller that gets false must still advance -- see the length rules in the
    // implementation.
    bool decodeOne(const uint8_t* data, duint size, duint address, cs_insn* insn) const;

    Architecture* mArchitecture = nullptr;
    QHash<ENCODETYPE, DataInstructionInfo> mDataInstMap;
    bool mLongDataInst = false;
    bool mUseRunTrace = false;
    EncodeMap* mEncodeMap = nullptr;
    CodeFoldingHelper* mCodeFoldingManager = nullptr;

    csh mArm64Handle = 0;
    csh mX86Handle = 0;

    // Reused across decodes: cs_malloc allocates one of these and cs_disasm_iter fills it in
    // place, which is the allocation-free path Capstone documents for exactly this use.
    cs_insn* mScratch = nullptr;
};
