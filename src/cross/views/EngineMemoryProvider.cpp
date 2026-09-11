#include "EngineMemoryProvider.h"

#include <cstring>

void EngineMemoryProvider::setEngine(DbgEngine* engine)
{
    mEngine = engine;
}

bool EngineMemoryProvider::read(const duint addr, void* dest, const duint size)
{
    if(!mEngine || !dest || size == 0)
        return false;
    return mEngine->MemRead(mEngine->impl, addr, dest, size) == DbgStatus_Ok;
}

bool EngineMemoryProvider::getRange(const duint addr, duint& base, duint& size)
{
    if(!mEngine)
        return false;

    uint64_t foundBase = 0;
    uint64_t foundSize = 0;
    if(mEngine->MemFindBaseAddr(mEngine->impl, addr, &foundBase, &foundSize) != DbgStatus_Ok)
        return false;

    base = static_cast<duint>(foundBase);
    size = static_cast<duint>(foundSize);
    return true;
}

bool EngineMemoryProvider::isCodePtr(const duint addr)
{
    return mEngine != nullptr && mEngine->MemIsCodePtr(mEngine->impl, addr);
}

bool EngineMemoryProvider::isValidPtr(const duint addr)
{
    return mEngine != nullptr && mEngine->MemIsValidPtr(mEngine->impl, addr);
}

bool EngineMemoryProvider::write(const duint addr, const void* src, const duint size)
{
    if(!mEngine || !src || size == 0)
        return false;
    return mEngine->MemWrite(mEngine->impl, addr, src, size) == DbgStatus_Ok;
}

bool EngineMemoryProvider::modBaseFromAddr(const duint addr, duint& base)
{
    if(!mEngine)
        return false;

    uint64_t found = 0;
    if(mEngine->ModBaseFromAddr(mEngine->impl, addr, &found) != DbgStatus_Ok)
        return false;

    base = static_cast<duint>(found);
    return true;
}

bool EngineMemoryProvider::modNameFromAddr(const duint addr, char* buf, const duint bufSize,
                                           const bool extension)
{
    if(!mEngine || buf == nullptr || bufSize == 0)
        return false;

    if(mEngine->ModNameFromAddr(mEngine->impl, addr, buf, bufSize) != DbgStatus_Ok)
        return false;

    // The bridge's callers ask for the name with or without its extension. A dylib's name is
    // `libcurl.4.dylib`, so "without the extension" means dropping the last suffix only --
    // truncating at the first dot would turn it into `libcurl`, which names nothing.
    if(!extension)
    {
        if(char* const dot = std::strrchr(buf, '.'))
            *dot = '\0';
    }
    return true;
}
