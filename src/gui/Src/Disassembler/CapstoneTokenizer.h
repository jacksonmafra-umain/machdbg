#pragma once

#include <QString>
#include <vector>

#include <capstone/capstone.h>

// The token types themselves still come from ZydisTokenizer.h until milestone 6 task 5 retires
// Zydis and moves them: two tokenizers producing "the same" classification from two copies of
// an enum is exactly how they stop being the same.
#include "ZydisTokenizer.h"

class Architecture;

// The classified token stream, built from cs_detail.
//
// NOT by re-lexing Capstone's printed text, and the spec (section 7) says why: re-lexing is
// cheaper and throws the classification away. An immediate becomes an anonymous number and a
// register becomes a word, which leaves the highlighter with nothing to highlight by. The
// structured operands are the whole reason this is worth writing.
//
// What it does not classify, it marks Uncategorized rather than guessing. A token that says "I
// do not know what this is" costs a colour; one that guesses wrong costs the reader's trust in
// every other colour on the line.
class CapstoneTokenizer
{
public:
    using TokenType = ZydisTokenizer::TokenType;
    using SingleToken = ZydisTokenizer::SingleToken;
    using TokenValue = ZydisTokenizer::TokenValue;
    using InstructionToken = ZydisTokenizer::InstructionToken;

    CapstoneTokenizer(int maxModuleSize, Architecture* architecture);

    // Fills `instruction` from an already-decoded Capstone instruction. Taking the decode rather
    // than doing it means the disassembler decodes once: QCapstone::DisassembleAt needs the same
    // cs_insn for the length and the text.
    //
    // The handle comes with it because cs_reg_name needs one -- register names are a property of
    // the open handle, not of the instruction.
    bool TokenizeInstruction(csh handle, const cs_insn& insn, InstructionToken& instruction) const;

private:
    void emitMnemonic(const cs_insn& insn, InstructionToken& out) const;
    void emitArm64Operands(csh handle, const cs_insn& insn, InstructionToken& out) const;
    void emitX86Operands(csh handle, const cs_insn& insn, InstructionToken& out) const;

    void emitRegister(csh handle, unsigned reg, TokenType type, InstructionToken& out) const;
    // Named emitToken, not emit: Qt defines `emit` as a macro, so a method of that name
    // cannot exist in a header any Qt translation unit includes.
    static void emitToken(TokenType type, const QString& text, InstructionToken& out);
    static void emitValue(TokenType type, duint value, int size, InstructionToken& out);

    // Which register class a name belongs to, so the highlighter can colour an xmm differently
    // from a general-purpose register. Capstone has no "register class" query that covers both
    // architectures, so this reads the name -- which is what the name is for.
    static TokenType registerTypeOf(const QString& name);

    Architecture* mArchitecture = nullptr;
    int mMaxModuleSize = 0;
};
