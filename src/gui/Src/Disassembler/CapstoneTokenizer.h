#pragma once

// 16 bytes: the longest instruction either architecture has -- 15 on x86-64, 4 on arm64. It was
// zydis_wrapper.h's constant and outlived that header, because the length belongs to the
// machine rather than to any disassembler.
#ifndef MAX_DISASM_BUFFER
#define MAX_DISASM_BUFFER 16
#endif

#include <capstone/capstone.h>
#include <Utils/RichTextPainter.h>
#include <Configuration.h>
#include <map>
#include <QHash>
#include <QtCore>
#include "Architecture.h"

class CapstoneTokenizer
{
public:
    enum class TokenType
    {
        //filling
        Comma = 0,
        Space,
        ArgumentSpace,
        MemoryOperatorSpace,
        //general instruction parts
        Prefix,
        Uncategorized,
        //mnemonics
        MnemonicNormal,
        MnemonicPushPop,
        MnemonicCall,
        MnemonicRet,
        MnemonicCondJump,
        MnemonicUncondJump,
        MnemonicNop,
        MnemonicFar,
        MnemonicInt3,
        MnemonicUnusual,
        //values
        Address, //jump/call destinations or displacements inside memory
        Value,
        TraceNewValue,
        //memory
        MemorySize,
        MemorySegment,
        MemoryBrackets,
        MemoryStackBrackets,
        MemoryBaseRegister,
        MemoryIndexRegister,
        MemoryScale,
        MemoryOperator, //'+', '-' and '*'
        //registers
        GeneralRegister,
        FpuRegister,
        MmxRegister,
        XmmRegister,
        YmmRegister,
        ZmmRegister,
        //last
        Last
    };

    struct TokenValue
    {
        int size; //value size (in bytes), zero means no value
        duint value; //actual value

        TokenValue(int size, duint value) :
            size(size),
            value(value)
        {
        }

        TokenValue() :
            size(0),
            value(0)
        {
        }

        bool operator == (const TokenValue & rhs) const
        {
            return /*size == rhs.size &&*/ value == rhs.value;
        }
    };

    struct SingleToken
    {
        TokenType type; //token type
        QString text; //token text
        TokenValue value; //token value (if applicable)

        SingleToken() :
            type(TokenType::Uncategorized)
        {
        }

        SingleToken(TokenType type, const QString & text, const TokenValue & value) :
            type(type),
            text(text),
            value(value)
        {
        }

        SingleToken(TokenType type, const QString & text) :
            SingleToken(type, text, TokenValue())
        {
        }

        bool operator == (const SingleToken & rhs) const
        {
            return type == rhs.type && text == rhs.text && value == rhs.value;
        }
    };

    struct InstructionToken
    {
        std::vector<SingleToken> tokens; //list of tokens that form the instruction
        int x = 0; //x of the first character

        QString toString() const
        {
            QString text;
            for(const auto & token : tokens)
                text += token.text;
            return text;
        }
    };

    struct TokenColor
    {
        RichTextPainter::CustomRichTextFlags flags;
        QColor color;
        QColor backgroundColor;

        TokenColor(QString color, QString backgroundColor)
        {
            if(color.length() && backgroundColor.length())
            {
                this->flags = RichTextPainter::FlagAll;
                this->color = ConfigColor(color);
                this->backgroundColor = ConfigColor(backgroundColor);
            }
            else if(color.length())
            {
                this->flags = RichTextPainter::FlagColor;
                this->color = ConfigColor(color);
            }
            else if(backgroundColor.length())
            {
                this->flags = RichTextPainter::FlagBackground;
                this->backgroundColor = ConfigColor(backgroundColor);
            }
            else
                this->flags = RichTextPainter::FlagNone;
        }

        TokenColor()
            : TokenColor("", "")
        {
        }
    };

    CapstoneTokenizer(int maxModuleLength, Architecture* architecture);

    // Fills `instruction` from an already-decoded Capstone instruction, rather than decoding
    // here. QCapstone needs the same cs_insn for the length and the text, so decoding twice
    // would be both slower and a chance for the two answers to differ.
    //
    // The handle comes with it because cs_reg_name needs one: register names belong to the open
    // handle, not to the instruction.
    bool TokenizeInstruction(csh handle, const cs_insn & insn, InstructionToken & instruction);
    bool TokenizeData(const QString & datatype, const QString & data, InstructionToken & instruction);
    void UpdateConfig();
    void UpdateArchitecture();
    void SetConfig(bool bUppercase, bool bTabbedMnemonic, bool bArgumentSpaces, bool bHidePointerSizes, bool bHideNormalSegments, bool bMemorySpaces, bool bNoHighlightOperands, bool bNoCurrentModuleText, DisasmValueNotationType ValueNotation);

    static void UpdateColors();
    static void UpdateStringPool();
    static void TokenToRichText(const InstructionToken & instr, RichTextPainter::List & richTextList, const SingleToken* highlightToken);
    static bool TokenFromX(const InstructionToken & instr, SingleToken & token, int x, CachedFontMetrics* fontMetrics);
    static bool IsHighlightableToken(const SingleToken & token);
    static bool TokenEquals(const SingleToken* a, const SingleToken* b, bool ignoreSize = true);
    static void addColorName(TokenType type, QString color, QString backgroundColor);
    static void addStringsToPool(const QString & regs);
    static bool tokenTextPoolEquals(const QString & a, const QString & b);
    static TokenColor getTokenColor(TokenType type);

    static void TokenizeTraceRegister(const char* reg, duint oldValue, duint newValue, std::vector<SingleToken> & tokens);
    static void TokenizeTraceMemory(duint address, duint oldValue, duint newValue, std::vector<SingleToken> & tokens);


private:
    // The Capstone operand walkers, in place of the Zydis ones this class was built around.
    void tokenizeArm64Operands(csh handle, const cs_insn & insn);
    void tokenizeX86Operands(csh handle, const cs_insn & insn);
    void tokenizeRegister(csh handle, unsigned reg, TokenType type);
    static TokenType registerTypeOf(const QString & name);

    Architecture* mArchitecture;
    bool mSuccess = false;
    bool mIsNop = false;
    InstructionToken mInst;
    TokenType mMnemonicType = TokenType::Uncategorized;
    int mMaxModuleLength = -1;
    // The address of the instruction being tokenized. Recorded because printValue needs to
    // know whether a value lies inside the instruction's own module, and the tokenizer no
    // longer owns a decoder to ask where it is.
    duint mCurrentAddress = 0;

    bool mUppercase = false;
    bool mTabbedMnemonic = false;
    bool mArgumentSpaces = false;
    bool mHidePointerSizes = false;
    bool mHideNormalSegments = false;
    bool mMemorySpaces = false;
    bool mNoHighlightOperands = false;
    bool mNoCurrentModuleText = false;
    DisasmValueNotationType mValueNotation = DisasmValueNotationNone;

    void addToken(TokenType type, QString text, const TokenValue & value);
    void addToken(TokenType type, const QString & text);
    void addMemoryOperator(char operatorText);
    QString printValue(const TokenValue & value, bool expandModule) const;

    static QHash<QString, int> gStringPool;
    static int gPoolId;
    void tokenizeMnemonic(const cs_insn & insn);
    void tokenizeMnemonic(TokenType type, const QString & mnemonic);
};
