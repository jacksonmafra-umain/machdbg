#pragma once

#include <mach/mach.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <MachBug/api/machbug_api.h>

namespace MachBug
{
    // The engine's breakpoint table: what the user asked for, what was written into the target to
    // achieve it, and enough to undo it exactly.
    //
    // Guarded, unlike most of this engine: SetBreakpoint() arrives on whatever thread a UI runs on
    // while the loop thread is servicing exceptions and consulting the same table to classify
    // them. The mutex is this class's own and deliberately not Debugger's command mutex -- that
    // one exists to hand a decision to a parked handleException(), and a breakpoint query taking
    // it while such a decision is pending would deadlock the two against each other.
    //
    // The table knows nothing about *why* a breakpoint is wanted and nothing about exceptions; it
    // owns bytes and slots. Classifying a stop as a hit belongs to the exception loop, which asks
    // Find().
    class Breakpoints
    {
    public:
        struct Entry
        {
            uint64_t address = 0;
            DbgBreakpointKind kind = DbgBreakpointKind_Software;
            uint32_t size = 0;              // watchpoints only; 0 for the others
            bool enabled = true;            // what the user asked for
            bool armed = false;             // whether the target currently carries it
            int slot = -1;                  // hardware only; -1 while unassigned
            std::vector<uint8_t> original;  // software only: the bytes the target shipped
        };

        // Adds and arms. Refuses a second breakpoint at an address that already has one -- the
        // second Add would save the first one's trap as "the original bytes" and restore the trap
        // itself on removal, leaving the target permanently patched.
        bool Add(mach_port_t task, DbgArch arch, uint64_t address, DbgBreakpointKind kind,
                 uint32_t size, std::string* error);

        // Disarms and forgets. Restoring the original bytes is part of removal rather than a
        // separate step a caller can skip or forget.
        bool Remove(mach_port_t task, DbgArch arch, uint64_t address, std::string* error);

        // Arms or disarms in the target without forgetting the entry -- what a UI's enable
        // checkbox means. Disabled means the target no longer carries the trap, not merely that a
        // flag says so: a caller cannot tell those apart from the outside, and only one of them
        // stops the target.
        bool SetEnabled(mach_port_t task, DbgArch arch, uint64_t address, bool enabled,
                        std::string* error);

        // The entry at `address`, or nothing. The exception loop's question.
        std::optional<Entry> Find(uint64_t address) const;

        std::vector<Entry> All() const;

        // Takes every armed breakpoint out of the target without forgetting it: the first half of
        // the restore-step-re-arm cycle, and what a detaching engine owes the target.
        bool RestoreAll(mach_port_t task, DbgArch arch, std::string* error);

        // Puts every enabled breakpoint back. The second half of that cycle.
        bool ArmAll(mach_port_t task, DbgArch arch, std::string* error);

    private:
        // Both assume the caller holds mMutex.
        bool armLocked(mach_port_t task, DbgArch arch, Entry& entry, std::string* error);
        bool disarmLocked(mach_port_t task, DbgArch arch, Entry& entry, std::string* error);

        mutable std::mutex mMutex;
        std::vector<Entry> mEntries;
    };
}
