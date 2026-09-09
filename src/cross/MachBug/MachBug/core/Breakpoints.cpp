#include <MachBug/core/Breakpoints.h>

#include <MachBug/arch/Arch.h>
#include <MachBug/memory/Memory.h>

#include <algorithm>
#include <cstdio>

namespace MachBug
{
    namespace
    {
        std::string hex(const uint64_t value)
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "0x%llx",
                          static_cast<unsigned long long>(value));
            return buffer;
        }
    }

    bool Breakpoints::armLocked(const mach_port_t task, const DbgArch arch, Entry& entry,
                                std::string* error)
    {
        if(entry.armed)
            return true;

        // Hardware kinds are tasks 5 and 6. Until then they are refused by name rather than
        // quietly turned into software breakpoints: a caller that asked for hardware asked
        // precisely for the target's bytes to be left alone.
        if(entry.kind != DbgBreakpointKind_Software)
        {
            if(error)
                *error = "hardware breakpoints and watchpoints are not implemented yet";
            return false;
        }

        const arch::Trap trap = arch::SoftwareTrap(arch);
        if(trap.size == 0 || trap.bytes == nullptr)
        {
            if(error)
                *error = "no software trap encoding for this architecture";
            return false;
        }

        // Saved before the write, and only on the transition into armed: re-saving on a second
        // arm would record the trap as the original and make removal write the trap back.
        entry.original.assign(trap.size, 0);
        if(!memory::Read(task, entry.address, entry.original.data(), trap.size, error))
        {
            entry.original.clear();
            return false;
        }

        if(!memory::Write(task, entry.address, trap.bytes, trap.size, error))
        {
            entry.original.clear();
            return false;
        }

        entry.armed = true;
        return true;
    }

    bool Breakpoints::disarmLocked(const mach_port_t task, const DbgArch arch, Entry& entry,
                                   std::string* error)
    {
        (void)arch;
        if(!entry.armed)
            return true;

        if(entry.kind != DbgBreakpointKind_Software)
        {
            if(error)
                *error = "hardware breakpoints and watchpoints are not implemented yet";
            return false;
        }

        if(entry.original.empty())
        {
            // Armed with nothing saved is a bug in this class rather than a failure of the
            // target, and restoring nothing would leave the trap in place forever. Say which.
            if(error)
                *error = "breakpoint at " + hex(entry.address) +
                         " is armed but has no saved bytes to restore";
            return false;
        }

        if(!memory::Write(task, entry.address, entry.original.data(), entry.original.size(),
                          error))
            return false;

        entry.armed = false;
        entry.original.clear();
        return true;
    }

    bool Breakpoints::Add(const mach_port_t task, const DbgArch arch, const uint64_t address,
                          const DbgBreakpointKind kind, const uint32_t size, std::string* error)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        const auto existing = std::find_if(mEntries.begin(), mEntries.end(),
            [address](const Entry& entry) { return entry.address == address; });
        if(existing != mEntries.end())
        {
            if(error)
                *error = "a breakpoint is already set at " + hex(address);
            return false;
        }

        if(kind == DbgBreakpointKind_Software)
        {
            const arch::Trap trap = arch::SoftwareTrap(arch);
            if(trap.alignment > 1 && (address % trap.alignment) != 0)
            {
                if(error)
                    *error = "cannot set a software breakpoint at " + hex(address) + ": this "
                             "architecture needs " + std::to_string(trap.alignment) +
                             "-byte alignment, and a trap written across an instruction boundary "
                             "corrupts two instructions and traps at neither";
                return false;
            }
        }

        Entry entry;
        entry.address = address;
        entry.kind = kind;
        entry.size = size;
        entry.enabled = true;

        if(!armLocked(task, arch, entry, error))
            return false;

        mEntries.push_back(std::move(entry));
        return true;
    }

    bool Breakpoints::Remove(const mach_port_t task, const DbgArch arch, const uint64_t address,
                             std::string* error)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        const auto found = std::find_if(mEntries.begin(), mEntries.end(),
            [address](const Entry& entry) { return entry.address == address; });
        if(found == mEntries.end())
        {
            if(error)
                *error = "no breakpoint at " + hex(address);
            return false;
        }

        if(!disarmLocked(task, arch, *found, error))
            return false;

        mEntries.erase(found);
        return true;
    }

    bool Breakpoints::SetEnabled(const mach_port_t task, const DbgArch arch,
                                 const uint64_t address, const bool enabled, std::string* error)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        const auto found = std::find_if(mEntries.begin(), mEntries.end(),
            [address](const Entry& entry) { return entry.address == address; });
        if(found == mEntries.end())
        {
            if(error)
                *error = "no breakpoint at " + hex(address);
            return false;
        }

        // The flag moves only if the target agreed: reporting a breakpoint as disabled while its
        // trap is still in the target is the one state a caller has no way to detect.
        if(enabled)
        {
            if(!armLocked(task, arch, *found, error))
                return false;
        }
        else if(!disarmLocked(task, arch, *found, error))
        {
            return false;
        }

        found->enabled = enabled;
        return true;
    }

    std::optional<Breakpoints::Entry> Breakpoints::Find(const uint64_t address) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        const auto found = std::find_if(mEntries.begin(), mEntries.end(),
            [address](const Entry& entry) { return entry.address == address; });
        if(found == mEntries.end())
            return std::nullopt;
        return *found;
    }

    std::vector<Breakpoints::Entry> Breakpoints::All() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mEntries;
    }

    bool Breakpoints::RestoreAll(const mach_port_t task, const DbgArch arch, std::string* error)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        // Every entry is attempted even after one fails, and the first diagnostic is what comes
        // back: stopping at the first failure would leave the remaining traps in the target,
        // which is the outcome this function exists to prevent.
        bool ok = true;
        std::string firstError;
        for(Entry& entry : mEntries)
        {
            std::string entryError;
            if(!disarmLocked(task, arch, entry, &entryError))
            {
                ok = false;
                if(firstError.empty())
                    firstError = entryError;
            }
        }
        if(!ok && error)
            *error = firstError;
        return ok;
    }

    bool Breakpoints::ArmAll(const mach_port_t task, const DbgArch arch, std::string* error)
    {
        std::lock_guard<std::mutex> lock(mMutex);

        bool ok = true;
        std::string firstError;
        for(Entry& entry : mEntries)
        {
            if(!entry.enabled)
                continue;
            std::string entryError;
            if(!armLocked(task, arch, entry, &entryError))
            {
                ok = false;
                if(firstError.empty())
                    firstError = entryError;
            }
        }
        if(!ok && error)
            *error = firstError;
        return ok;
    }
}
