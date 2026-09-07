#include <ElfBug/process/Process.h>
#include <sys/ptrace.h>
#include <cerrno>

namespace ElfBug
{
    bool Process::SetBreakpoint(const ptr address, bool singleshot, const SoftwareType type)
    {
        BreakpointKey key{BreakpointType::Software, address};
        if(breakpoints.count(key))
            return false;

        errno = 0;
        const long origWord = ptrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(address), nullptr);
        if(origWord == -1 && errno != 0)
            return false;

        const auto origByte = static_cast<uint8_t>(origWord & 0xFF);
        const long patchedWord = (origWord & ~0xFFL) | 0xCC;

        if(ptrace(PTRACE_POKEDATA, pid, reinterpret_cast<void*>(address), reinterpret_cast<void*>(patchedWord)) == -1)
            return false;

        BreakpointInfo info;
        info.address = address;
        info.singleshot = singleshot;
        info.type = BreakpointType::Software;
        info.internal.software.type = type;
        info.internal.software.oldbytes[0] = origByte;
        info.internal.software.newbytes[0] = 0xCC;
        info.internal.software.size = 1;

        const auto it = breakpoints.emplace(key, info).first;
        softwareBreakpointReferences[address] = it;

        return true;
    }

    bool Process::SetBreakpoint(const ptr address, const BreakpointCallback & cbBreakpoint, bool singleshot, const SoftwareType type)
    {
        if(!SetBreakpoint(address, singleshot, type))
            return false;

        const BreakpointKey key{BreakpointType::Software, address};
        breakpointCallbacks[key] = cbBreakpoint;
        return true;
    }

    bool Process::DeleteBreakpoint(const ptr address)
    {
        const BreakpointKey key{BreakpointType::Software, address};
        const auto it = breakpoints.find(key);
        if(it == breakpoints.end())
            return false;

        const uint8_t origByte = it->second.internal.software.oldbytes[0];
        errno = 0;
        const long word = ptrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void*>(address), nullptr);
        if(word == -1 && errno != 0)
            return false;

        const long restoredWord = (word & ~0xFFL) | origByte;
        if(ptrace(PTRACE_POKEDATA, pid, reinterpret_cast<void*>(address), reinterpret_cast<void*>(restoredWord)) == -1)
            return false;

        softwareBreakpointReferences.erase(address);
        breakpointCallbacks.erase(key);
        breakpoints.erase(it);
        return true;
    }

    bool Process::SetMemoryBreakpoint(const ptr address, const ptr size, const MemoryType type, bool singleshot)
    {
        (void)address;
        (void)size;
        (void)type;
        (void)singleshot;
        return false;
    }

    bool Process::SetMemoryBreakpoint(const ptr address, const ptr size, const BreakpointCallback & cbBreakpoint, const MemoryType type, bool singleshot)
    {
        (void)address;
        (void)size;
        (void)cbBreakpoint;
        (void)type;
        (void)singleshot;
        return false;
    }

    bool Process::DeleteMemoryBreakpoint(ptr const address)
    {
        (void)address;
        return false;
    }

    void Process::StepOver(const StepCallback & cbStep)
    {
        (void)cbStep;
    }
}
