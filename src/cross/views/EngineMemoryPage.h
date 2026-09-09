#pragma once

#include <Memory/MemoryPage.h>

#include <MachBug/api/machbug_api.h>

// A MemoryPage backed by the engine's vtable, so the vendored HexDump can show a live target's
// memory without knowing what a target is. MemoryPage is virtual precisely for this: hex_viewer
// backs it with a file, and this backs it with MemRead/MemWrite.
//
// Reads and writes go through the vtable rather than MachBug::memory directly, deliberately: a
// view that talked to the engine's internals would be a view that only works with this backend,
// and the vtable exists so it does not have to be.
class EngineMemoryPage : public MemoryPage
{
    Q_OBJECT

public:
    explicit EngineMemoryPage(QObject* parent = nullptr);

    // Points the page at a window of the target's address space. Called on every stop, since a
    // resumed target may have unmapped whatever was on screen.
    void setEngine(DbgEngine* engine);

    bool read(void* dest, dsint rva, duint size) const override;
    bool write(const void* src, dsint rva, duint size) override;

private:
    DbgEngine* mEngine = nullptr;
};
