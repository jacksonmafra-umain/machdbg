#pragma once

/*
This should probably take some inspiration from Zydis:
- Address space min/max (64 vs 32 bit basically)
- Disassembly architecture (likely should return a reference to a disassembler)

*/
class Architecture
{
public:
    // Which instruction set, as opposed to how wide its addresses are. The two questions below
    // cannot answer it: a 64-bit arm64 target and a 64-bit x86-64 one give the same answers to
    // both, and a disassembler serving both architectures has to be told which one it is
    // looking at. Added by machdbg's milestone 6, when Capstone replaced Zydis and one
    // disassembler started serving both.
    enum class Cpu
    {
        X86,
        Arm64,
    };

    virtual ~Architecture() = default;

    // TODO: replace this with something about address space
    virtual bool disasm64() const = 0;
    virtual bool addr64() const = 0;

    // Defaulted rather than pure: the three x86-only implementations in this repository
    // (hex_viewer, minidump, the Linux debugger) are correct as they stand, and making them
    // answer a question they have only one answer to would be noise. The debugger's own window
    // overrides it from the target's architecture.
    virtual Cpu cpu() const
    {
        return Cpu::X86;
    }
};
