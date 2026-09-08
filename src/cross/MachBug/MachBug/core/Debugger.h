#pragma once

#include <sys/types.h>
#include <mach/mach.h>
#include <memory>
#include <string>

#include <MachBug/types/MachBug.h>
#include <MachBug/types/Global.h>
#include <MachBug/process/Process.h>

namespace MachBug
{
    // Milestone 2 task 3: launch a process suspended and take its task port. Nothing else yet
    // -- there is no exception loop, so Start/Continue/StepInto/Pause/Stop and the rest of
    // ElfBug::Debugger's virtual callback surface do not exist on this class. Task 4 adds the
    // mach_msg receive loop and the callbacks it needs to report through; this class is
    // deliberately not yet ElfBug-complete.
    class Debugger
    {
    public:
        Debugger();
        virtual ~Debugger();

        // Launches szFilePath suspended (POSIX_SPAWN_START_SUSPENDED) and takes its task port
        // via task_for_pid. The child exists but has executed no instruction of its own when
        // this returns -- that window is deliberate, it is what lets Task 4 install exception
        // ports before the target can run and race the debugger. Returns false, leaving
        // GetPid()/GetTaskPort() at 0/MACH_PORT_NULL, if the path cannot be executed or the
        // task port cannot be obtained; a partially-launched (suspended, task-for-pid-denied)
        // child is killed rather than leaked.
        bool Init(const char* szFilePath, const char* const* argv = nullptr);

        pid_t GetPid() const;
        mach_port_t GetTaskPort() const;

        // Kills a child that Init() launched but that was never resumed -- there is no Start()
        // yet at this stage of the milestone, so there is no debug loop to Stop() instead.
        // Returns false if there is no launched child to terminate.
        bool Terminate();

    protected:
        // Called with a diagnostic whenever Init() fails. The default implementation discards
        // it; a subclass (or Task 4/5's recording harness) overrides it to observe why.
        virtual void cbInternalError(const std::string & error);

    private:
        bool spawnSuspended(const char* szFilePath, const char* const* argv, pid_t* outPid);

        std::unique_ptr<Process> mProcess;
    };
}
