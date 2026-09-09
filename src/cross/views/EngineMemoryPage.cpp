#include "EngineMemoryPage.h"

#include <cstring>

EngineMemoryPage::EngineMemoryPage(QObject* parent)
    : MemoryPage(0, 0, parent)
{
}

void EngineMemoryPage::setEngine(DbgEngine* engine)
{
    mEngine = engine;
}

bool EngineMemoryPage::read(void* dest, const dsint rva, const duint size) const
{
    if(!mEngine || !dest || size == 0)
        return false;

    // Zeroed first, then filled as far as the target allows: a HexDump asks for a whole row at a
    // time, and a row that straddles the end of a mapping would otherwise show whatever this
    // process last left in the buffer -- other people's memory presented as the target's.
    std::memset(dest, 0, size);
    return mEngine->MemRead(mEngine->impl, va(rva), dest, size) == DbgStatus_Ok;
}

bool EngineMemoryPage::write(const void* src, const dsint rva, const duint size)
{
    if(!mEngine || !src || size == 0)
        return false;
    return mEngine->MemWrite(mEngine->impl, va(rva), src, size) == DbgStatus_Ok;
}
