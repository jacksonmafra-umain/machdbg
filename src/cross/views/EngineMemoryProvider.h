#pragma once

#include <MachBug/api/machbug_api.h>

#include <Bridge.h>

// The engine, as the vendored widgets' bridge expects to see memory.
//
// This exists for a reason that is easy to miss and impossible to work around: the disassembly
// view calls setDrawDebugOnly(true), and AbstractTableView::paintEvent then refuses to paint a
// single cell unless DbgIsDebugging() is true. In this port that function answers "is a memory
// provider installed" -- so without one, a correctly built, correctly fed disassembly pane draws
// nothing at all and looks like a bug in the disassembler.
//
// It is also what makes the tokenizer's label and module lookups work: printValue asks the
// bridge whether an address has a label or belongs to a module, and the bridge asks this.
class EngineMemoryProvider : public MemoryProvider
{
public:
    // The engine may be null, which is how a provider is kept installed across a target that
    // has gone away: every query then fails rather than reading a dangling vtable.
    void setEngine(DbgEngine* engine);

    bool read(duint addr, void* dest, duint size) override;
    bool getRange(duint addr, duint& base, duint& size) override;
    bool isCodePtr(duint addr) override;
    bool isValidPtr(duint addr) override;
    bool write(duint addr, const void* src, duint size) override;
    bool modBaseFromAddr(duint addr, duint& base) override;
    bool modNameFromAddr(duint addr, char* buf, duint bufSize, bool extension) override;

private:
    DbgEngine* mEngine = nullptr;
};
