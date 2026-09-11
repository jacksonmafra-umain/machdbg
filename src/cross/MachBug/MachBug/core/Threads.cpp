#include <MachBug/core/Threads.h>

#include <algorithm>
#include <cstring>

namespace MachBug
{
    namespace
    {
        // The stable identity behind a thread port. THREAD_IDENTIFIER_INFO is the only thread
        // info flavor that carries one; everything else describes state that changes.
        uint64_t identityOf(const mach_port_t thread)
        {
            thread_identifier_info_data_t info{};
            mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
            if(thread_info(thread, THREAD_IDENTIFIER_INFO,
                           reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS)
                return 0;
            return info.thread_id;
        }
    }

    Threads::~Threads()
    {
        Reset();
    }

    Threads::Change Threads::Refresh(const mach_port_t task)
    {
        Change change;
        if(task == MACH_PORT_NULL)
            return change;

        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if(task_threads(task, &threads, &count) != KERN_SUCCESS)
            return change;

        std::vector<Entry> current;
        current.reserve(count);
        for(mach_msg_type_number_t i = 0; i < count; ++i)
        {
            const uint64_t id = identityOf(threads[i]);
            if(id == 0)
            {
                // A thread that cannot say who it is cannot be tracked: release the right rather
                // than hold one for an entry nothing can ever match.
                mach_port_deallocate(mach_task_self(), threads[i]);
                continue;
            }

            const auto known = std::find_if(mKnown.begin(), mKnown.end(),
                [id](const Entry& entry) { return entry.id == id; });
            if(known != mKnown.end())
            {
                // Already held. The fresh right is redundant, and keeping it instead of the old
                // one would invalidate a port a caller may be using during this very stop.
                mach_port_deallocate(mach_task_self(), threads[i]);
                current.push_back(*known);
                known->port = MACH_PORT_NULL;  // ownership moved into `current`
            }
            else
            {
                current.push_back(Entry{id, threads[i]});  // right kept
                if(mHaveBaseline)
                    change.appeared.push_back(id);
            }
        }
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                      count * sizeof(thread_act_t));

        // Whatever is left holding a port in mKnown was not seen this time: the thread is gone.
        for(Entry& entry : mKnown)
        {
            if(entry.port == MACH_PORT_NULL)
                continue;
            mach_port_deallocate(mach_task_self(), entry.port);
            if(mHaveBaseline)
                change.disappeared.push_back(entry.id);
        }

        mKnown = std::move(current);
        mHaveBaseline = true;
        return change;
    }

    mach_port_t Threads::PortFor(const uint64_t threadId) const
    {
        const auto found = std::find_if(mKnown.begin(), mKnown.end(),
            [threadId](const Entry& entry) { return entry.id == threadId; });
        return found == mKnown.end() ? MACH_PORT_NULL : found->port;
    }

    std::vector<uint64_t> Threads::Known() const
    {
        std::vector<uint64_t> ids;
        ids.reserve(mKnown.size());
        for(const Entry& entry : mKnown)
            ids.push_back(entry.id);
        return ids;
    }

    void Threads::Reset()
    {
        for(Entry& entry : mKnown)
        {
            if(entry.port != MACH_PORT_NULL)
                mach_port_deallocate(mach_task_self(), entry.port);
        }
        mKnown.clear();
        mHaveBaseline = false;
    }

    std::vector<mach_port_t> Threads::Ports() const
    {
        std::vector<mach_port_t> ports;
        ports.reserve(mKnown.size());
        for(const Entry& entry : mKnown)
            ports.push_back(entry.port);
        return ports;
    }

    const char* Threads::RunStateName(const int32_t runState)
    {
        switch(runState)
        {
        case TH_STATE_RUNNING:         return "running";
        case TH_STATE_STOPPED:         return "stopped";
        case TH_STATE_WAITING:         return "waiting";
        case TH_STATE_UNINTERRUPTIBLE: return "uninterruptible";
        case TH_STATE_HALTED:          return "halted";
        default:                       return "unknown";
        }
    }

    bool Threads::Describe(const uint64_t id, Detail* const out) const
    {
        if(out == nullptr)
            return false;

        const mach_port_t port = PortFor(id);
        if(port == MACH_PORT_NULL)
            return false;

        *out = Detail{};
        out->id = id;
        out->port = port;

        // THREAD_EXTENDED_INFO (flavor 5) is the only way to read another process's thread
        // names: pthread_getname_np reads the caller's own threads and has no cross-process
        // form. A thread that never named itself comes back with an empty pth_name, which is
        // most of them.
        thread_extended_info_data_t extended{};
        mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
        if(thread_info(port, THREAD_EXTENDED_INFO, reinterpret_cast<thread_info_t>(&extended),
                       &count) == KERN_SUCCESS)
        {
            // pth_name is a fixed buffer that need not be terminated when full.
            const char* const name = extended.pth_name;
            const std::size_t length = ::strnlen(name, sizeof(extended.pth_name));
            out->name.assign(name, length);
            out->runState = extended.pth_run_state;
        }
        else
        {
            // Fall back to the basic info, which every thread has: a list that loses a row
            // because one flavor was unavailable is worse than one with less detail in it.
            thread_basic_info_data_t basic{};
            mach_msg_type_number_t basicCount = THREAD_BASIC_INFO_COUNT;
            if(thread_info(port, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&basic),
                           &basicCount) != KERN_SUCCESS)
            {
                return false;
            }
            out->runState = basic.run_state;
        }

        out->runStateName = RunStateName(out->runState);
        return true;
    }
}
