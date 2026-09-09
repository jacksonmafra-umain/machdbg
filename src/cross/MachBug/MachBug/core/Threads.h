#pragma once

#include <mach/mach.h>

#include <cstdint>
#include <vector>

namespace MachBug
{
    // The target's threads: which exist, what changed since the last look, and a port for each
    // one that stays valid while it lives.
    //
    // Polled at each stop rather than delivered by a notification, because macOS gives a debugger
    // no thread-lifecycle event on the exception port it owns: a thread simply exists the next
    // time anyone asks. A stop is also the only moment the engine may touch another thread's
    // state, which is what this exists to enable -- debug registers are per-thread here, so a
    // hardware breakpoint set before a thread was born has to be applied to it once it appears.
    //
    // IDENTITY IS THE SYSTEM THREAD ID, NOT THE PORT NAME. That distinction was measured, not
    // assumed: naming threads by the port task_threads returns reported three creations for a
    // three-thread target that had only spawned two, and an id that no longer resolved. Each
    // task_threads call mints fresh send rights, whose names differ from the previous call's, so
    // port names look like new threads on every poll and die as soon as they are released.
    // THREAD_IDENTIFIER_INFO's thread_id is stable for the life of the thread and is what the C
    // API's threadId means.
    //
    // One send right per live thread is held for exactly that reason: it keeps the port -- and
    // therefore the mapping from id to port -- valid until the thread goes away, at which point
    // the right is released. Holding rights for dead threads would keep their kernel structures
    // alive and eventually exhaust this process's port table, which is the failure this class is
    // careful to avoid on the other side.
    class Threads
    {
    public:
        struct Change
        {
            std::vector<uint64_t> appeared;
            std::vector<uint64_t> disappeared;
        };

        ~Threads();

        // Reads the target's current threads and returns what changed since the previous call.
        // The first call establishes the baseline and reports nothing: the threads a target
        // already had when the engine arrived were not created under its watch, and reporting
        // them as appearances would put creations that never happened into a caller's list.
        Change Refresh(mach_port_t task);

        // The port for a thread id, or MACH_PORT_NULL. The right is this object's; a caller uses
        // the port for the duration of a stop and does not release it.
        mach_port_t PortFor(uint64_t threadId) const;

        std::vector<uint64_t> Known() const;

        // Releases every held right and forgets everything, for a Debugger pointed at a new
        // target.
        void Reset();

    private:
        struct Entry
        {
            uint64_t id = 0;
            mach_port_t port = MACH_PORT_NULL;
        };

        std::vector<Entry> mKnown;
        bool mHaveBaseline = false;
    };
}
