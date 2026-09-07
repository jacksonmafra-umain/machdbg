#include <ElfBug/core/Debugger.h>

namespace ElfBug
{
    void Debugger::createThreadEvent(pid_t tid)
    {
        if(!mProcess)
            return;

        {
            std::unique_lock lock(mProcessMutex);
            mProcess->threads.emplace(tid, std::make_unique<Thread>(tid));
        }
        cbCreateThreadEvent(tid);
    }

    void Debugger::exitThreadEvent(const pid_t tid)
    {
        if(!mProcess)
            return;

        cbExitThreadEvent(tid);

        std::unique_lock lock(mProcessMutex);
        if(mThread && mThread->tid == tid)
            mThread = nullptr;

        mProcess->threads.erase(tid);
    }
}
