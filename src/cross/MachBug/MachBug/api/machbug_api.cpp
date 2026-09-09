// Bridges MachBug::Debugger (core/Debugger.h) to the C DbgEngine vtable machbug_api.h declares,
// the way ElfBug's api/elfbug_api.cpp bridges ElfBug::Debugger to its own C surface -- read that
// file first, it is this one's model. The two engines diverge here exactly where machbug_api.h
// itself diverges from elfbug_api.h: ElfBug exports plain C functions taking an ElfBugDebugger*
// directly; MachBug hands the caller a `DbgEngine*` -- a struct of function pointers plus an
// opaque `impl` -- because milestone 0 needs a remote backend to be able to fill the same vtable
// later without the adapter above it ever knowing the difference. MachBugCreate() therefore
// allocates two objects, not one: the MachBugEngine (the actual Debugger subclass, referenced
// only through `impl`) and the DbgEngine vtable object returned to the caller.
//
// UNIMPLEMENTED VTABLE ENTRIES -- THE DECISION THIS FILE'S HEADER COMMENT IN THE TASK BRIEF ASKS
// FOR EXPLICITLY: every memory, register, module and breakpoint entry is milestone 3 and 4's
// work, not this one's. None of them is left null. Each is wired to a small stub that returns
// DbgStatus_NotSupported (or, for the four entries whose return type cannot carry a DbgStatus --
// MemIsCodePtr, MemIsValidPtr and IsBreakpointEffective return bool; GetRegisterDescs returns a
// pointer; GetHwBreakpointSlots returns a count -- the closest equivalent: false, nullptr/*count
// = 0, and 0, respectively). A null function pointer crashes the instant an adapter reaches for
// it, which is a poor way for that adapter to learn a feature is not ready yet; a stub that
// returns a named "not supported" status is discoverable at the call site, in the same units
// (DbgStatus) every other entry in this vtable already reports through. This is the convention
// milestones 3 and 4 replace one entry at a time, not a placeholder that changes shape later --
// callers written against "check the DbgStatus" from day one need no adjustment when a stub
// becomes real.
//
// SECOND DECISION: the first exception every launch or attach ever produces -- dyld's own
// post-exec notification on the launch path, the SIGCONT-turned-exception PT_ATTACHEXC's own
// attach produces on the attach path (see Debugger::Attach()'s comment) -- is left stopped, not
// auto-resumed. MachBug::Debugger parks a target there exactly like any other exception, and
// this layer does not decide on the caller's behalf that the answer is always "run": a stop is a
// withheld reply, and the caller is what decides when it ends, the same way ElfBug's own
// cbSystemBreakpoint() (elfbug_api.cpp) leaves the target stopped and lets its caller call
// Continue(). An engine that resumed here automatically would give an adapter a different first
// stop depending on which backend happened to be behind the vtable, which is precisely the
// divergence machbug_api.h's vtable exists to prevent. See MachBugEngine::cbSystemBreakpoint()
// below.
#include <MachBug/api/machbug_api.h>
#include <MachBug/core/Debugger.h>

#include <atomic>
#include <string>

namespace
{
    // Detail for the last DbgStatus returned on the calling thread -- DbgLastErrorString()'s
    // backing store. Per-thread because MachBug::Debugger's own contract is that Continue(),
    // StepInto(), Pause() and Stop() are called from a thread other than the one blocked in
    // Start(); a single shared string would let one thread's diagnostic clobber another's for no
    // reason. Never a raw Mach kern_return_t (see machbug_api.h) -- it always holds the same
    // named diagnostic cbInternalError() below already produced.
    thread_local std::string tlsLastError;

    struct MachBugEngine final : MachBug::Debugger
    {
        DbgEngineCallbacks cb{};

    protected:
        void cbInternalError(const std::string & error) override
        {
            tlsLastError = error;
            if(cb.onError)
                cb.onError(error.c_str(), cb.userdata);
        }

        void cbCreateProcessEvent(const pid_t pid) override
        {
            // MachBug does not yet track a target's entry point -- that needs module/dyld
            // enumeration, milestone 5's work, not this one's. 0 here is "unknown", the same
            // convention GetArch() below documents for the architecture it cannot yet inspect
            // either.
            if(cb.onCreateProcess)
                cb.onCreateProcess(pid, 0, cb.userdata);
        }

        void cbExitProcessEvent(const int exitCode) override
        {
            if(cb.onExitProcess)
                cb.onExitProcess(exitCode, cb.userdata);
        }

        void cbSystemBreakpoint() override
        {
            // No auto-resume: matches elfbug_api.cpp's own cbSystemBreakpoint(), which fires the
            // callback and leaves the target stopped rather than deciding for the caller. A stop
            // is a withheld reply, and the caller decides when it ends -- that default is what
            // the whole vtable rests on, launch or attach, this milestone or a later one with
            // real breakpoints. A caller that wants to run past this stop calls Continue()
            // itself (on the vtable, from inside this very callback or later, on another
            // thread) -- exactly as ElfBug's own callers already do, and exactly what a real
            // caller above this engine does regardless of which backend filled the vtable. An
            // engine that decided this instead would give an adapter a different first stop
            // depending on which backend it happened to be talking to, which is precisely the
            // divergence the vtable exists to prevent.
            if(cb.onSystemBreakpoint)
                cb.onSystemBreakpoint(cb.userdata);
        }

        void cbResumed() override
        {
            if(cb.onResumed)
                cb.onResumed(cb.userdata);
        }

        void cbStep() override
        {
            if(cb.onStep)
                cb.onStep(cb.userdata);
        }

        void cbException(const uint32_t type, const uint64_t address) override
        {
            if(cb.onException)
                cb.onException(type, address, cb.userdata);
        }

        // onBreakpoint, onPaused, onDebugString, onLoadModule, onUnloadModule, onThreadCreate and
        // onThreadExit have no MachBug::Debugger event to wire them to yet: breakpoints and
        // per-thread state are milestones 3/4, modules are milestone 5, and Debugger::Pause()
        // (unlike ElfBug's Pause(), which surfaces through the waitpid loop that already exists
        // there) task_suspend()s the target directly with nothing in this milestone's loop to
        // notice and report it from. Left unfired here rather than fired with fabricated
        // arguments; a caller relying on any of them before its milestone lands would be relying
        // on a callback this contract never promised silently to invoke.
    };

    MachBugEngine* toEngine(void* impl)
    {
        return static_cast<MachBugEngine*>(impl);
    }

    // Distinguishes a denied task_for_pid -- machbug_api.h documents DbgStatus_NotPermitted
    // specifically for it -- from any other launch/attach failure. Checked immediately after
    // Init()/Attach() returns false, before anything else has a chance to make another
    // task_for_pid call on this engine and overwrite LastTaskForPidResult().
    DbgStatus classifyLaunchFailure(const MachBugEngine* engine)
    {
        return engine->LastTaskForPidResult() != KERN_SUCCESS ? DbgStatus_NotPermitted
                                                               : DbgStatus_Failed;
    }

    DbgStatus vtStart(void* impl, const DbgLaunchSpec* spec)
    {
        if(!impl || !spec)
            return DbgStatus_InvalidArgument;

        MachBugEngine* engine = toEngine(impl);

        bool launched;
        if(spec->attachPid > 0)
        {
            // spec->path, spec->argv, spec->envp and spec->workingDirectory are meaningless for
            // an attach and are ignored here, matching DbgLaunchSpec's own comment ("attachPid: 0
            // to launch rather than attach").
            launched = engine->Attach(spec->attachPid);
        }
        else
        {
            if(!spec->path)
                return DbgStatus_InvalidArgument;

            // spec->envp and spec->workingDirectory are not yet honoured: MachBug::Debugger::Init()
            // has no support for either (it inherits this process's own environment and current
            // directory) -- adding them is not this milestone's lifecycle work and is left for
            // whichever later task first needs a launched target with a controlled environment.
            launched = engine->Init(spec->path, spec->argv);
        }

        if(!launched)
            return classifyLaunchFailure(engine);

        // Start() blocks for as long as the target lives (or until Stop() ends the loop from
        // another thread) -- matching machbug_api.h's own comment on this vtable entry ("Start
        // blocks and owns the event loop"), unchanged from what MachBug::Debugger::Start() has
        // documented since Task 4.
        return engine->Start() ? DbgStatus_Ok : DbgStatus_Failed;
    }

    // GetPid() alone is not enough to gate Continue/StepInto/Pause/Stop below: MachBug::Debugger
    // does not reset mProcess when its loop ends on its own (a clean exit, a crash, or a prior
    // Stop()) -- GetPid() keeps reporting the now-dead target's pid until Terminate()/the
    // destructor runs, exactly so a caller can still ask "what was I just debugging" after the
    // fact (LastExitCode()-style bookkeeping upstream relies on the same thing). IsRunning() is
    // what actually answers "is Start()'s loop still going" -- true from the start of Start()
    // until exceptionLoop() returns, unaffected by whether mProcess is still around. A command
    // reaching a pid whose loop has already ended gets the enum machbug_api.h reserves for
    // exactly this (DbgStatus_TargetExited), not a misleading DbgStatus_Ok that implies the
    // command did something.
    DbgStatus classifyCommandTarget(const MachBugEngine* engine)
    {
        if(!engine || engine->GetPid() <= 0)
            return DbgStatus_NotAttached;
        if(!engine->IsRunning())
            return DbgStatus_TargetExited;
        return DbgStatus_Ok;
    }

    DbgStatus vtContinue(void* impl)
    {
        MachBugEngine* engine = toEngine(impl);
        const DbgStatus status = classifyCommandTarget(engine);
        if(status != DbgStatus_Ok)
            return status;
        engine->Continue();
        return DbgStatus_Ok;
    }

    DbgStatus vtStepInto(void* impl)
    {
        MachBugEngine* engine = toEngine(impl);
        const DbgStatus status = classifyCommandTarget(engine);
        if(status != DbgStatus_Ok)
            return status;
        engine->StepInto();
        return DbgStatus_Ok;
    }

    DbgStatus vtPause(void* impl)
    {
        MachBugEngine* engine = toEngine(impl);
        const DbgStatus status = classifyCommandTarget(engine);
        if(status != DbgStatus_Ok)
            return status;
        engine->Pause();
        return DbgStatus_Ok;
    }

    DbgStatus vtStop(void* impl)
    {
        MachBugEngine* engine = toEngine(impl);
        const DbgStatus status = classifyCommandTarget(engine);
        if(status != DbgStatus_Ok)
            return status;
        engine->Stop();
        return DbgStatus_Ok;
    }

    pid_t vtGetPid(void* impl)
    {
        MachBugEngine* engine = toEngine(impl);
        return engine ? engine->GetPid() : 0;
    }

    DbgArch vtGetArch(void* /*impl*/)
    {
        // Placeholder until milestone 3 gives this engine a real way to inspect a target's
        // architecture (that needs register/module access this milestone does not implement --
        // see this file's header comment on the null-vs-stub decision). Reports the host's own
        // architecture, which is correct for the only configuration this milestone's own tests
        // exercise -- a native, non-translated target -- and wrong for one running under
        // Rosetta, which stays out of scope until milestone 3.
#if defined(__arm64__) || defined(__aarch64__)
        return DbgArch_Arm64;
#elif defined(__x86_64__)
        return DbgArch_X86_64;
#else
        return DbgArch_Unknown;
#endif
    }

    // Everything below is milestone 3 (memory, registers) or milestone 4 (breakpoints) work --
    // see this file's header comment for why each is a stub returning a named "not supported"
    // result rather than a null pointer.

    DbgStatus vtGetRegisters(void* /*impl*/, uint64_t /*threadId*/, DbgRegisters* /*out*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtSetRegister(void* /*impl*/, uint64_t /*threadId*/, const char* /*name*/,
                             uint64_t /*value*/)
    {
        return DbgStatus_NotSupported;
    }

    const DbgRegisterDesc* vtGetRegisterDescs(void* /*impl*/, DbgArch /*arch*/, uint32_t* count)
    {
        if(count)
            *count = 0;
        return nullptr;
    }

    DbgStatus vtMemRead(void* /*impl*/, uint64_t /*addr*/, void* /*dest*/, uint64_t /*size*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtMemWrite(void* /*impl*/, uint64_t /*addr*/, const void* /*src*/, uint64_t /*size*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtMemFindBaseAddr(void* /*impl*/, uint64_t /*addr*/, uint64_t* /*base*/,
                                 uint64_t* /*size*/)
    {
        return DbgStatus_NotSupported;
    }

    bool vtMemIsCodePtr(void* /*impl*/, uint64_t /*addr*/)
    {
        return false;
    }

    bool vtMemIsValidPtr(void* /*impl*/, uint64_t /*addr*/)
    {
        return false;
    }

    DbgStatus vtMemEnumRegions(void* /*impl*/, DbgMemoryRegion* /*out*/, uint32_t /*capacity*/,
                                uint32_t* count)
    {
        if(count)
            *count = 0;
        return DbgStatus_NotSupported;
    }

    DbgStatus vtModBaseFromAddr(void* /*impl*/, uint64_t /*addr*/, uint64_t* /*base*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtModNameFromAddr(void* /*impl*/, uint64_t /*addr*/, char* /*buf*/,
                                 uint64_t /*bufSize*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtSetBreakpoint(void* /*impl*/, DbgBreakpointKind /*kind*/, uint64_t /*addr*/,
                               uint32_t /*size*/)
    {
        return DbgStatus_NotSupported;
    }

    DbgStatus vtDeleteBreakpoint(void* /*impl*/, uint64_t /*addr*/)
    {
        return DbgStatus_NotSupported;
    }

    bool vtIsBreakpointEffective(void* /*impl*/, uint64_t /*addr*/)
    {
        return false;
    }

    uint32_t vtGetHwBreakpointSlots(void* /*impl*/)
    {
        return 0;
    }
}

extern "C" {

    DbgEngine* MachBugCreate(const DbgEngineCallbacks* callbacks)
    {
        auto* engine = new MachBugEngine();
        if(callbacks)
            engine->cb = *callbacks;

        auto* vtable = new DbgEngine{};
        vtable->impl = engine;

        vtable->Start = vtStart;
        vtable->Continue = vtContinue;
        vtable->StepInto = vtStepInto;
        vtable->Pause = vtPause;
        vtable->Stop = vtStop;

        vtable->GetRegisters = vtGetRegisters;
        vtable->SetRegister = vtSetRegister;
        vtable->GetRegisterDescs = vtGetRegisterDescs;
        vtable->GetPid = vtGetPid;
        vtable->GetArch = vtGetArch;

        vtable->MemRead = vtMemRead;
        vtable->MemWrite = vtMemWrite;
        vtable->MemFindBaseAddr = vtMemFindBaseAddr;
        vtable->MemIsCodePtr = vtMemIsCodePtr;
        vtable->MemIsValidPtr = vtMemIsValidPtr;
        vtable->MemEnumRegions = vtMemEnumRegions;

        vtable->ModBaseFromAddr = vtModBaseFromAddr;
        vtable->ModNameFromAddr = vtModNameFromAddr;

        vtable->SetBreakpoint = vtSetBreakpoint;
        vtable->DeleteBreakpoint = vtDeleteBreakpoint;
        vtable->IsBreakpointEffective = vtIsBreakpointEffective;
        vtable->GetHwBreakpointSlots = vtGetHwBreakpointSlots;

        return vtable;
    }

    void MachBugDestroy(DbgEngine* engine)
    {
        if(!engine)
            return;
        delete toEngine(engine->impl);
        delete engine;
    }

    const char* DbgLastErrorString(void)
    {
        return tlsLastError.c_str();
    }

} // extern "C"
