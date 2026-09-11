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
#include <MachBug/arch/Arch.h>
#include <MachBug/core/Debugger.h>
#include <MachBug/core/Modules.h>
#include <MachBug/core/Threads.h>
#include <MachBug/memory/Memory.h>

#include <mach/vm_prot.h>
#include <sys/sysctl.h>
#include <sys/proc.h>

#include <atomic>
#include <string>
#include <vector>

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

        void cbBreakpoint(uint64_t address) override
        {
            if(cb.onBreakpoint)
                cb.onBreakpoint(address, cb.userdata);
        }

        void cbThreadCreate(uint64_t threadId) override
        {
            if(cb.onThreadCreate)
                cb.onThreadCreate(threadId, cb.userdata);
        }

        void cbThreadExit(uint64_t threadId) override
        {
            if(cb.onThreadExit)
                cb.onThreadExit(threadId, cb.userdata);
        }

        void cbException(const uint32_t type, const uint64_t address) override
        {
            if(cb.onException)
                cb.onException(type, address, cb.userdata);
        }

        // onPaused, onDebugString, onLoadModule and onUnloadModule have no MachBug::Debugger
        // event to wire them to yet: modules are milestone 5, and Debugger::Pause()
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

    DbgArch vtGetArch(void* impl)
    {
        // The host's architecture unless the target is translated. A Rosetta process carries
        // P_TRANSLATED (0x00020000, sys/proc.h) in its p_flag, which is the only thing that
        // distinguishes it from a native one without reading its Mach-O header -- and reading
        // that needs the main image's base address, which is milestone 5's work.
        //
        // Note the limit this reports rather than hides: knowing a target is x86-64 is not the
        // same as being able to read it. A translated target's *thread state* is the translator's
        // arm64 state, so GetRegisters() on one cannot answer with x86-64 registers, and
        // MachBug::arch says so in its diagnostic. Reporting the architecture correctly is what
        // lets a caller understand that refusal instead of seeing a contradiction.
        if(auto* engine = toEngine(impl); engine && engine->GetPid() > 0)
        {
            struct kinfo_proc info{};
            size_t length = sizeof(info);
            int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, engine->GetPid() };
            if(sysctl(name, 4, &info, &length, nullptr, 0) == 0 && length > 0 &&
               (info.kp_proc.p_flag & P_TRANSLATED) != 0)
                return DbgArch_X86_64;
        }
#if defined(__arm64__) || defined(__aarch64__)
        return DbgArch_Arm64;
#elif defined(__x86_64__)
        return DbgArch_X86_64;
#else
        return DbgArch_Unknown;
#endif
    }

    // Registers and memory, milestone 3's work. Everything below *these* is still milestone 4
    // (breakpoints) or 5 (modules) work -- see this file's header comment for why each of those
    // is a stub returning a named "not supported" result rather than a null pointer.
    //
    // Each entry here is the same three moves: recover the engine, call into MachBug::arch or
    // MachBug::memory, and turn a false return plus its diagnostic into a DbgStatus plus
    // tlsLastError. The status vocabulary, so it stays consistent across all nine:
    //   NotAttached      -- no engine, or no target at all
    //   InvalidArgument  -- a target, but the request cannot name what it asks for: nothing is
    //                       stopped (so there are no registers), the thread id names no thread,
    //                       the register name is not in the table, or an output pointer is null
    //   Failed           -- the target is there and the request is well formed, but the kernel
    //                       refused it (an unmapped address, a page that cannot be made writable)
    // A status alone cannot tell those apart, which is why every failure also leaves the
    // diagnostic behind DbgLastErrorString().
    // Both remaining causes -- nothing is stopped, or the thread id/register name names nothing
    // -- are InvalidArgument: the target exists and the kernel was never asked anything, so
    // Failed would be wrong. What tells them apart is the diagnostic, which is why it is stored
    // here rather than summarised into a status that cannot carry it.
    DbgStatus registerFailure(const MachBugEngine* engine, const std::string& error)
    {
        tlsLastError = error;
        if(!engine || engine->GetPid() <= 0)
            return DbgStatus_NotAttached;
        return DbgStatus_InvalidArgument;
    }

    DbgStatus vtGetRegisters(void* impl, uint64_t threadId, DbgRegisters* out)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetPid() <= 0)
        {
            tlsLastError = "no target: launch or attach before reading registers";
            return DbgStatus_NotAttached;
        }
        if(!out)
        {
            tlsLastError = "no DbgRegisters to fill";
            return DbgStatus_InvalidArgument;
        }

        std::string error;
        if(!MachBug::arch::Read(vtGetArch(impl), engine->ResolveThread(threadId), out, &error))
            return registerFailure(engine, error);
        return DbgStatus_Ok;
    }

    DbgStatus vtSetRegister(void* impl, uint64_t threadId, const char* name, uint64_t value)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetPid() <= 0)
        {
            tlsLastError = "no target: launch or attach before writing a register";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(!MachBug::arch::Write(vtGetArch(impl), engine->ResolveThread(threadId), name, value,
                                 &error))
            return registerFailure(engine, error);
        return DbgStatus_Ok;
    }

    const DbgRegisterDesc* vtGetRegisterDescs(void* /*impl*/, DbgArch arch, uint32_t* count)
    {
        // No engine needed, and deliberately no target either: a table is data, so a view can
        // lay out its rows before anything is launched.
        return MachBug::arch::Descriptors(arch, count);
    }

    DbgStatus vtMemRead(void* impl, uint64_t addr, void* dest, uint64_t size)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before reading memory";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(!MachBug::memory::Read(engine->GetTaskPort(), addr, dest, size, &error))
        {
            tlsLastError = error;
            return dest == nullptr || size == 0 ? DbgStatus_InvalidArgument : DbgStatus_Failed;
        }
        return DbgStatus_Ok;
    }

    DbgStatus vtMemWrite(void* impl, uint64_t addr, const void* src, uint64_t size)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before writing memory";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(!MachBug::memory::Write(engine->GetTaskPort(), addr, src, size, &error))
        {
            tlsLastError = error;
            return src == nullptr || size == 0 ? DbgStatus_InvalidArgument : DbgStatus_Failed;
        }
        return DbgStatus_Ok;
    }

    DbgStatus vtMemFindBaseAddr(void* impl, uint64_t addr, uint64_t* base, uint64_t* size)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before asking about an address";
            return DbgStatus_NotAttached;
        }

        MachBug::memory::Region region{};
        std::string error;
        if(!MachBug::memory::RegionOf(engine->GetTaskPort(), addr, &region, &error))
        {
            tlsLastError = error;
            return DbgStatus_Failed;
        }

        // mach_vm_region answers with the first region at *or above* the address it is given, so
        // an address in an unmapped hole comes back described by the next mapping up. Reporting
        // that as this address's base would be a lie a caller cannot detect.
        if(addr < region.base || addr >= region.base + region.size)
        {
            tlsLastError = "no mapped region contains this address";
            return DbgStatus_Failed;
        }

        if(base)
            *base = region.base;
        if(size)
            *size = region.size;
        return DbgStatus_Ok;
    }

    // The two predicates return bool and so cannot carry a status; they still leave a diagnostic
    // behind, because "false" from either of them is exactly the answer a caller most often
    // wants explained.
    bool vtMemIsCodePtr(void* impl, uint64_t addr)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before asking about an address";
            return false;
        }

        MachBug::memory::Region region{};
        std::string error;
        if(!MachBug::memory::RegionOf(engine->GetTaskPort(), addr, &region, &error))
        {
            tlsLastError = error;
            return false;
        }
        if(addr < region.base || addr >= region.base + region.size)
        {
            tlsLastError = "no mapped region contains this address";
            return false;
        }
        return (region.protection & VM_PROT_EXECUTE) != 0;
    }

    bool vtMemIsValidPtr(void* impl, uint64_t addr)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before asking about an address";
            return false;
        }

        MachBug::memory::Region region{};
        std::string error;
        if(!MachBug::memory::RegionOf(engine->GetTaskPort(), addr, &region, &error))
        {
            tlsLastError = error;
            return false;
        }
        // Mapped *and* readable: a region with no VM_PROT_READ is a guard page, and a caller
        // asking whether a pointer is valid is asking whether it can be dereferenced.
        return addr >= region.base && addr < region.base + region.size &&
               (region.protection & VM_PROT_READ) != 0;
    }

    DbgStatus vtMemEnumRegions(void* impl, DbgMemoryRegion* out, uint32_t capacity,
                                uint32_t* count)
    {
        if(count)
            *count = 0;

        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before enumerating regions";
            return DbgStatus_NotAttached;
        }
        if(!out || capacity == 0)
        {
            tlsLastError = "no room to write regions into";
            return DbgStatus_InvalidArgument;
        }

        // Copied field by field rather than reinterpreted: MachBug::memory::Region is the
        // engine's own shape and DbgMemoryRegion is the contract's, and nothing should make them
        // silently depend on having identical layouts.
        std::vector<MachBug::memory::Region> regions(capacity);
        const uint32_t found = MachBug::memory::EnumRegions(engine->GetTaskPort(), regions.data(),
                                                            capacity);

        // The correlation happens here rather than in memory/: that layer reports what the
        // kernel says about an address space, and what dyld loaded where is a different source
        // of truth. Keeping them apart is what lets the parser and the map be tested without
        // each other.
        const std::vector<MachBug::Modules::Image> modules = engine->LoadedModules();
        const auto moduleAt = [&modules](const uint64_t address) -> uint64_t {
            for(const MachBug::Modules::Image& image : modules)
            {
                if(image.size != 0 && address >= image.loadAddress &&
                   address < image.loadAddress + image.size)
                {
                    return image.loadAddress;
                }
            }
            return 0;
        };

        for(uint32_t i = 0; i < found; ++i)
        {
            out[i] = DbgMemoryRegion{regions[i].base, regions[i].size, regions[i].protection,
                                     regions[i].maxProtection, regions[i].userTag,
                                     regions[i].depth, regions[i].tagName,
                                     moduleAt(regions[i].base)};
        }
        if(count)
            *count = found;
        if(found == 0)
        {
            tlsLastError = "no regions could be read from this target";
            return DbgStatus_Failed;
        }
        return DbgStatus_Ok;
    }

    DbgStatus vtThreadEnum(void* impl, DbgThread* out, const uint32_t capacity, uint32_t* count)
    {
        if(count)
            *count = 0;

        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before enumerating threads";
            return DbgStatus_NotAttached;
        }
        if(out == nullptr || capacity == 0)
        {
            tlsLastError = "no room to write threads into";
            return DbgStatus_InvalidArgument;
        }

        // What the engine saw at the last stop, not a fresh task_threads: the thread list a
        // caller is drawing belongs to the same moment as the registers and the memory beside
        // it, and re-enumerating here would mint ports nothing releases.
        const std::vector<uint64_t> known = engine->KnownThreads();
        const mach_port_t stopped = engine->StoppedThread();

        uint32_t written = 0;
        for(const uint64_t id : known)
        {
            if(written == capacity)
                break;

            MachBug::Threads::Detail detail;
            if(!engine->DescribeThread(id, &detail))
                continue;

            DbgThread entry{};
            entry.id = detail.id;
            entry.runState = static_cast<uint32_t>(detail.runState);
            entry.runStateName = detail.runStateName;
            entry.isStoppedThread = detail.port == stopped && stopped != MACH_PORT_NULL;
            std::snprintf(entry.name, sizeof(entry.name), "%s", detail.name.c_str());

            // Through arch::Read rather than thread_get_state here: that is where pointer
            // authentication is stripped on arm64 (spec section 6), and a second place that
            // read a program counter would be a second place to forget it.
            DbgRegisters regs{};
            std::string readError;
            if(MachBug::arch::Read(vtGetArch(impl), detail.port, &regs, &readError))
            {
                entry.pc = vtGetArch(impl) == DbgArch_Arm64 ? regs.arm64.pc : regs.x86_64.rip;
            }

            out[written++] = entry;
        }

        if(count)
            *count = written;
        return DbgStatus_Ok;
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

    DbgStatus vtSetBreakpoint(void* impl, const DbgBreakpointKind kind, const uint64_t addr,
                               const uint32_t size)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before setting a breakpoint";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(engine->BreakpointTable().Add(engine->GetTaskPort(), vtGetArch(impl), addr, kind, size,
                                          &error))
            return DbgStatus_Ok;

        // The table's own refusals reach a C caller through DbgLastErrorString() rather than
        // being flattened away: "this machine has 6 execution slots and all 6 are in use" and
        // "the hardware encodes watchpoint sizes of 1, 2, 4 and 8 bytes only" are the difference
        // between a UI that can tell its user what to do and one that reports a bare failure.
        tlsLastError = error;

        // An address or size the engine could see was wrong before it touched the target,
        // against a target that refused the write. A caller retries one of those and not the
        // other.
        const bool argumentWasWrong =
            error.find("alignment") != std::string::npos ||
            error.find("multiple of the size") != std::string::npos ||
            error.find("bytes only") != std::string::npos ||
            error.find("already set at") != std::string::npos;
        return argumentWasWrong ? DbgStatus_InvalidArgument : DbgStatus_Failed;
    }

    DbgStatus vtDeleteBreakpoint(void* impl, const uint64_t addr)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before deleting a breakpoint";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(engine->BreakpointTable().Remove(engine->GetTaskPort(), vtGetArch(impl), addr, &error))
            return DbgStatus_Ok;

        tlsLastError = error;
        return error.find("no breakpoint at") != std::string::npos ? DbgStatus_InvalidArgument
                                                                   : DbgStatus_Failed;
    }

    DbgStatus vtSetBreakpointEnabled(void* impl, const uint64_t addr, const bool enabled)
    {
        auto* engine = toEngine(impl);
        if(!engine || engine->GetTaskPort() == MACH_PORT_NULL)
        {
            tlsLastError = "no target: launch or attach before enabling a breakpoint";
            return DbgStatus_NotAttached;
        }

        std::string error;
        if(engine->BreakpointTable().SetEnabled(engine->GetTaskPort(), vtGetArch(impl), addr,
                                                 enabled, &error))
            return DbgStatus_Ok;

        tlsLastError = error;
        return error.find("no breakpoint at") != std::string::npos ? DbgStatus_InvalidArgument
                                                                   : DbgStatus_Failed;
    }

    bool vtIsBreakpointEffective(void* impl, const uint64_t addr)
    {
        auto* engine = toEngine(impl);
        if(!engine)
            return false;

        // Effective means the target is actually carrying it right now -- armed, not merely
        // remembered. A caller cannot tell those apart from the outside, and only one of them
        // stops the target.
        const auto entry = engine->BreakpointTable().Find(addr);
        return entry.has_value() && entry->armed;
    }

    uint32_t vtGetHwBreakpointSlots(void* impl)
    {
        // The machine's count, from sysctl on arm64 and from the architecture on x86-64 -- never
        // the sixteen that arm_debug_state64_t's arrays are wide. A caller that believes it has
        // sixteen offers a seventh hardware breakpoint and then has to explain a refusal it
        // cannot account for.
        return MachBug::arch::SlotCounts(vtGetArch(impl)).exec;
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

        vtable->ThreadEnum = vtThreadEnum;
        vtable->ModBaseFromAddr = vtModBaseFromAddr;
        vtable->ModNameFromAddr = vtModNameFromAddr;

        vtable->SetBreakpoint = vtSetBreakpoint;
        vtable->DeleteBreakpoint = vtDeleteBreakpoint;
        vtable->SetBreakpointEnabled = vtSetBreakpointEnabled;
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
