#include <MachBug/core/Breakpoints.h>

#include <MachBug/arch/Arch.h>
#include <MachBug/memory/Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

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

    void Breakpoints::SetThreadSource(ThreadSource source)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mThreadSource = std::move(source);
    }

    int Breakpoints::allocateExecSlotLocked(const DbgArch arch) const
    {
        const uint32_t slots = arch::SlotCounts(arch).exec;
        for(uint32_t slot = 0; slot < slots; ++slot)
        {
            const bool taken = std::any_of(mEntries.begin(), mEntries.end(),
                [slot](const Entry& entry) {
                    return entry.kind == DbgBreakpointKind_HwExec &&
                           entry.slot == static_cast<int>(slot);
                });
            if(!taken)
                return static_cast<int>(slot);
        }
        return -1;
    }

    arch::DebugSlots Breakpoints::hardwareSlotsLocked(const DbgArch arch) const
    {
        arch::DebugSlots slots;
        slots.exec.assign(arch::SlotCounts(arch).exec, 0);
        for(const Entry& entry : mEntries)
        {
            if(entry.kind != DbgBreakpointKind_HwExec || !entry.armed || entry.slot < 0)
                continue;
            if(static_cast<uint32_t>(entry.slot) < slots.exec.size())
                slots.exec[static_cast<std::size_t>(entry.slot)] = entry.address;
        }
        return slots;
    }

    bool Breakpoints::applyHardwareLocked(const DbgArch arch, std::string* error) const
    {
        if(!mThreadSource)
        {
            if(error)
                *error = "this breakpoint table has no way to reach the target's threads, and a "
                         "hardware breakpoint is written into threads rather than into memory";
            return false;
        }

        const arch::DebugSlots slots = hardwareSlotsLocked(arch);
        const std::vector<mach_port_t> threads = mThreadSource();
        if(threads.empty())
        {
            if(error)
                *error = "the target has no threads to write debug registers to";
            return false;
        }

        // Every thread is attempted even after one fails, and the first diagnostic is what comes
        // back. A partial application is still better than stopping halfway: the threads that
        // took the write do carry the breakpoint, and the ones that did not are named by the
        // failure rather than silently skipped.
        bool ok = true;
        std::string firstError;
        for(const mach_port_t thread : threads)
        {
            std::string threadError;
            if(!arch::ApplyDebugState(arch, thread, slots, &threadError))
            {
                ok = false;
                if(firstError.empty())
                    firstError = threadError;
            }
        }
        if(!ok && error)
            *error = firstError;
        return ok;
    }

    bool Breakpoints::ApplyToThread(const DbgArch arch, const mach_port_t thread,
                                    std::string* error) const
    {
        std::lock_guard<std::mutex> lock(mMutex);

        // A table with no hardware breakpoints in it does not touch the thread at all. Writing
        // an all-zero slot table would be harmless in effect and wrong in principle: this engine
        // would be clearing debug registers it never set, on every thread a target ever creates.
        const bool anyHardware = std::any_of(mEntries.begin(), mEntries.end(),
            [](const Entry& entry) { return entry.kind == DbgBreakpointKind_HwExec; });
        if(!anyHardware)
            return true;

        return arch::ApplyDebugState(arch, thread, hardwareSlotsLocked(arch), error);
    }

    bool Breakpoints::armLocked(const mach_port_t task, const DbgArch arch, Entry& entry,
                                std::string* error)
    {
        if(entry.armed)
            return true;

        if(entry.kind == DbgBreakpointKind_HwExec)
        {
            // Marked armed before the write, because the slot table is built from what the
            // entries say -- and put back if the write does not happen, so the table never
            // claims a breakpoint the threads do not carry.
            entry.armed = true;
            if(!applyHardwareLocked(arch, error))
            {
                entry.armed = false;
                return false;
            }
            return true;
        }

        // Watchpoints are task 6. Refused by name rather than quietly turned into an execution
        // breakpoint, which would stop on the wrong event entirely.
        if(entry.kind != DbgBreakpointKind_Software)
        {
            if(error)
                *error = "watchpoints are not implemented yet";
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
        if(!entry.armed)
            return true;

        if(entry.kind == DbgBreakpointKind_HwExec)
        {
            entry.armed = false;
            if(!applyHardwareLocked(arch, error))
            {
                entry.armed = true;
                return false;
            }
            return true;
        }

        if(entry.kind != DbgBreakpointKind_Software)
        {
            if(error)
                *error = "watchpoints are not implemented yet";
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

        if(kind == DbgBreakpointKind_HwExec)
        {
            entry.slot = allocateExecSlotLocked(arch);
            if(entry.slot < 0)
            {
                const uint32_t slots = arch::SlotCounts(arch).exec;
                if(error)
                    *error = "cannot set a hardware breakpoint at " + hex(address) + ": this "
                             "machine has " + std::to_string(slots) + " execution slots and all " +
                             std::to_string(slots) + " are in use. A hardware breakpoint is not "
                             "quietly downgraded to a software one, because a caller that asked "
                             "for hardware asked for the target's bytes to be left alone";
                return false;
            }
        }

        // In the table before it is armed, and taken back out if arming fails. A hardware
        // breakpoint is written from what the table says, so an entry that arms before it is in
        // the table arms an empty slot set -- which is exactly nothing, silently.
        mEntries.push_back(std::move(entry));
        if(!armLocked(task, arch, mEntries.back(), error))
        {
            mEntries.pop_back();
            return false;
        }
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

    bool Breakpoints::Disarm(const mach_port_t task, const DbgArch arch, const uint64_t address,
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
        return disarmLocked(task, arch, *found, error);
    }

    bool Breakpoints::Arm(const mach_port_t task, const DbgArch arch, const uint64_t address,
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
        // A breakpoint the user disabled stays out of the target: the resume cycle re-arms what
        // it took out, and it must not resurrect what someone switched off in between.
        if(!found->enabled)
            return true;
        return armLocked(task, arch, *found, error);
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
