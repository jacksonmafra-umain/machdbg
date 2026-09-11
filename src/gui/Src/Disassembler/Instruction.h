#pragma once

#include <QByteArray>
#include <QString>
#include <vector>

#include "CapstoneTokenizer.h"

// One decoded instruction, as the disassembly view consumes it.
//
// Moved out of QZydis.h when Zydis was retired (milestone 6 task 5): the struct outlived the
// engine that used to fill it, and leaving it in a header named after that engine would have
// made the retirement look incomplete to everyone who opened it afterwards.
//
// prefixSize, opcodeSize, group1Size..group3Size and vectorElementType are filled by nobody
// since that retirement. Capstone has no equivalent of the Zydis byte-grouping and
// vector-element APIs, and formatOpcodeString uses the group sizes only to insert separators
// between prefix, opcode and operand bytes in the opcode column -- so they render as one
// unseparated byte string instead, which is a contained cosmetic loss on x86-64 and meaningless
// on arm64 where every instruction is four bytes. Kept rather than deleted because the fields
// are part of the shape the view and the trace reader agree on.
// How an instruction touches a register, for Instruction_t::regsReferenced. Carried over from
// the Zydis wrapper's vocabulary when it was retired, because the field's meaning did not
// change with the decoder that fills it.
//
// Implicit and Explicit are declared but never set on this port: Capstone's cs_regs_access does
// not separate them (measured in milestone 6 task 3), and inventing the distinction would be
// worse than omitting it. They stay so that the flag values keep meaning what every consumer
// already believes they mean.
enum RegisterAccess : uint8_t
{
    RegisterAccessNone     = 0,
    RegisterAccessRead     = 1 << 0,
    RegisterAccessWrite    = 1 << 1,
    RegisterAccessImplicit = 1 << 2,
    RegisterAccessExplicit = 1 << 3,
};

struct Instruction_t
{
    enum BranchType : uint8_t
    {
        None,
        Conditional,
        Unconditional,
        Call
    };

    duint rva = 0;
    duint branchDestination = 0;
    int length = 0;
    uint8_t vectorElementType[4];
    uint8_t prefixSize = 0;
    uint8_t opcodeSize = 0;
    uint8_t group1Size = 0;
    uint8_t group2Size = 0;
    uint8_t group3Size = 0;
    BranchType branchType = None;

    QString instStr;
    QByteArray dump;
    std::vector<std::pair<const char*, uint8_t>> regsReferenced;
    CapstoneTokenizer::InstructionToken tokens;

    Instruction_t()
    {
        memset(vectorElementType, 0, sizeof(vectorElementType));
    }
};

