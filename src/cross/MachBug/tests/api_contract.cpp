#include <catch2/catch_test_macros.hpp>

#include <MachBug/api/machbug_api.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <spawn.h>
#include <unistd.h>

// Not declared in <unistd.h> on macOS; see MachBug/core/Debugger.cpp's identical extern for why
// posix_spawn needs it passed explicitly as envp.
extern char** environ;

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

namespace
{
    // A stub engine proves the vtable is fillable without an implementation existing.
    struct StubEngine
    {
        int continueCalls = 0;
        bool started = false;
    };

    DbgStatus stubStart(void* impl, const DbgLaunchSpec* spec)
    {
        if(spec == nullptr || spec->path == nullptr)
            return DbgStatus_InvalidArgument;
        static_cast<StubEngine*>(impl)->started = true;
        return DbgStatus_Ok;
    }

    DbgStatus stubContinue(void* impl)
    {
        static_cast<StubEngine*>(impl)->continueCalls++;
        return DbgStatus_Ok;
    }
}

TEST_CASE("the vtable can be filled by an engine that does not exist yet")
{
    StubEngine stub;
    DbgEngine engine{};
    engine.impl = &stub;
    engine.Start = stubStart;
    engine.Continue = stubContinue;

    DbgLaunchSpec spec{};
    spec.path = "/usr/bin/true";

    REQUIRE(engine.Start(engine.impl, &spec) == DbgStatus_Ok);
    REQUIRE(stub.started);
    REQUIRE(engine.Continue(engine.impl) == DbgStatus_Ok);
    REQUIRE(stub.continueCalls == 1);
}

TEST_CASE("Start rejects a launch spec with no path")
{
    StubEngine stub;
    DbgLaunchSpec spec{};
    REQUIRE(stubStart(&stub, &spec) == DbgStatus_InvalidArgument);
    REQUIRE_FALSE(stub.started);
}

TEST_CASE("every architecture the port supports has an enum value")
{
    REQUIRE(DbgArch_Unknown == 0);
    REQUIRE(DbgArch_X86_64 != DbgArch_Arm64);
    REQUIRE(DbgArch_Arm64 != DbgArch_Arm64e);
    REQUIRE(DbgArch_I386 != DbgArch_X86_64);
}

TEST_CASE("registers are a tagged union, not a flat x86 layout")
{
    DbgRegisters regs{};
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0x100000000ull;
    REQUIRE(regs.arch == DbgArch_Arm64);
    REQUIRE(regs.arm64.pc == 0x100000000ull);

    regs.arch = DbgArch_X86_64;
    regs.x86_64.rip = 0x140001000ull;
    REQUIRE(regs.x86_64.rip == 0x140001000ull);
}

TEST_CASE("arm64 exposes the full general register file")
{
    DbgRegsArm64 regs{};
    // x0 through x30 inclusive.
    REQUIRE(sizeof(regs.x) / sizeof(regs.x[0]) == 31);
}

TEST_CASE("breakpoint kinds are architecture-neutral")
{
    // The point of the enum is that no caller ever names a debug register.
    REQUIRE(DbgBreakpointKind_Software != DbgBreakpointKind_HwExec);
    REQUIRE(DbgBreakpointKind_HwRead != DbgBreakpointKind_HwWrite);
}

TEST_CASE("a register descriptor table describes registers as data")
{
    static const DbgRegisterDesc sample[] = {
        { 0, "pc", 64, 0, DbgRegisterFlag_ProgramCounter },
        { 1, "sp", 64, 8, DbgRegisterFlag_StackPointer },
    };
    REQUIRE(std::string(sample[0].name) == "pc");
    REQUIRE(sample[0].flags == DbgRegisterFlag_ProgramCounter);
    REQUIRE(sample[1].bits == 64);
}

// From here on, the tests exercise the real engine MachBugCreate() returns -- not the stub
// above -- fulfilling the task brief's Step 2: "fills it for real". This is the first place
// MachBug::Debugger (core/Debugger.h) is driven only through the C vtable machbug_api.h
// declares, the same surface a real adapter above this engine would use.
namespace
{
    // Milestone 2's null-vs-stub decision (see machbug_api.cpp's header comment) means every
    // unimplemented vtable entry returns a value, never crashes -- worth a direct assertion of
    // its own, since it is the one property that decision exists to guarantee and nothing else
    // in this file checks it.
    DbgStatus vtStatus(DbgStatus s) { return s; }
}

TEST_CASE("unimplemented vtable entries are discoverable stubs, not null function pointers")
{
    DbgEngineCallbacks callbacks{};
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    // Every memory/register/module/breakpoint entry must be callable -- a null one would crash
    // this test instead of failing it cleanly.
    REQUIRE(engine->GetRegisters != nullptr);
    REQUIRE(engine->SetRegister != nullptr);
    REQUIRE(engine->GetRegisterDescs != nullptr);
    REQUIRE(engine->MemRead != nullptr);
    REQUIRE(engine->MemWrite != nullptr);
    REQUIRE(engine->MemFindBaseAddr != nullptr);
    REQUIRE(engine->MemIsCodePtr != nullptr);
    REQUIRE(engine->MemIsValidPtr != nullptr);
    REQUIRE(engine->MemEnumRegions != nullptr);
    REQUIRE(engine->ModBaseFromAddr != nullptr);
    REQUIRE(engine->ModNameFromAddr != nullptr);
    REQUIRE(engine->SetBreakpoint != nullptr);
    REQUIRE(engine->DeleteBreakpoint != nullptr);
    REQUIRE(engine->IsBreakpointEffective != nullptr);
    REQUIRE(engine->GetHwBreakpointSlots != nullptr);

    uint64_t dummy = 0;
    DbgRegisters regs{};
    REQUIRE(vtStatus(engine->GetRegisters(engine->impl, 0, &regs)) == DbgStatus_NotSupported);
    REQUIRE(vtStatus(engine->SetRegister(engine->impl, 0, "rip", 0)) == DbgStatus_NotSupported);

    uint32_t count = 123;
    REQUIRE(engine->GetRegisterDescs(engine->impl, DbgArch_Arm64, &count) == nullptr);
    REQUIRE(count == 0);

    REQUIRE(vtStatus(engine->MemRead(engine->impl, 0, &dummy, sizeof(dummy))) == DbgStatus_NotSupported);
    REQUIRE(vtStatus(engine->MemWrite(engine->impl, 0, &dummy, sizeof(dummy))) == DbgStatus_NotSupported);
    REQUIRE(vtStatus(engine->MemFindBaseAddr(engine->impl, 0, &dummy, &dummy)) == DbgStatus_NotSupported);
    REQUIRE_FALSE(engine->MemIsCodePtr(engine->impl, 0));
    REQUIRE_FALSE(engine->MemIsValidPtr(engine->impl, 0));

    count = 123;
    REQUIRE(vtStatus(engine->MemEnumRegions(engine->impl, nullptr, 0, &count)) == DbgStatus_NotSupported);
    REQUIRE(count == 0);

    REQUIRE(vtStatus(engine->ModBaseFromAddr(engine->impl, 0, &dummy)) == DbgStatus_NotSupported);
    char buf[8];
    REQUIRE(vtStatus(engine->ModNameFromAddr(engine->impl, 0, buf, sizeof(buf))) == DbgStatus_NotSupported);

    REQUIRE(vtStatus(engine->SetBreakpoint(engine->impl, DbgBreakpointKind_Software, 0, 1)) ==
            DbgStatus_NotSupported);
    REQUIRE(vtStatus(engine->DeleteBreakpoint(engine->impl, 0)) == DbgStatus_NotSupported);
    REQUIRE_FALSE(engine->IsBreakpointEffective(engine->impl, 0));
    REQUIRE(engine->GetHwBreakpointSlots(engine->impl) == 0);

    // Nothing was ever launched or attached -- Continue/StepInto/Pause/Stop must say so rather
    // than silently no-op.
    REQUIRE(engine->Continue(engine->impl) == DbgStatus_NotAttached);
    REQUIRE(engine->StepInto(engine->impl) == DbgStatus_NotAttached);
    REQUIRE(engine->Pause(engine->impl) == DbgStatus_NotAttached);
    REQUIRE(engine->Stop(engine->impl) == DbgStatus_NotAttached);

    MachBugDestroy(engine);
}

namespace
{
    // The vtable does not resume a target's first stop automatically (machbug_api.cpp's header
    // comment) -- a stop is a withheld reply, and the caller decides when it ends, the same way
    // ElfBug's own callers call Continue() themselves after onSystemBreakpoint. This is that
    // decision made explicit for a test that wants "just run it to completion", not
    // interactive control: call Continue() from inside the callback, exactly as a real caller
    // reaching for the same behavior would.
    struct ResumeOnSystemBreakpoint
    {
        DbgEngine* engine = nullptr;
    };

    void continueOnSystemBreakpoint(void* userdata)
    {
        auto* ctx = static_cast<ResumeOnSystemBreakpoint*>(userdata);
        if(ctx->engine)
            ctx->engine->Continue(ctx->engine->impl);
    }
}

// Adapted from the task brief's Step 2 test: launch exit_code_42 through the real engine and
// check the pid. exit_code_42 does nothing after being resumed (main() { return 42; }), so
// Start() -- blocking for the target's whole life, per machbug_api.h -- returns quickly, on this
// same thread, once the target has run to completion. Unlike the brief's literal snippet, this
// registers onSystemBreakpoint to call Continue() itself: the vtable leaves the target's first
// stop parked rather than resuming it for the caller (see machbug_api.cpp's header comment), so
// running to completion is this test's own explicit choice, not the engine's default.
TEST_CASE("the engine launches and runs a target through the C contract")
{
    ResumeOnSystemBreakpoint resume;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = continueOnSystemBreakpoint;
    callbacks.userdata = &resume;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);
    resume.engine = engine;

    DbgLaunchSpec spec{};
    spec.path = MACHBUG_TESTS_TARGETS_DIR "/exit_code_42";

    REQUIRE(engine->Start(engine->impl, &spec) == DbgStatus_Ok);
    REQUIRE(engine->GetPid(engine->impl) > 0);

    MachBugDestroy(engine);
}

// GetPid() alone is stale here on purpose (MachBug::Debugger does not reset mProcess just
// because its loop ended -- see machbug_api.cpp's classifyCommandTarget() comment), so this
// pins the gating that keeps that staleness from being mistaken for "the command succeeded":
// once Start() has returned, GetPid() still reports exit_code_42's now-dead pid, but every
// command below must report DbgStatus_TargetExited, not a misleading DbgStatus_Ok.
TEST_CASE("Continue/StepInto/Pause/Stop report TargetExited once the target has already exited")
{
    ResumeOnSystemBreakpoint resume;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = continueOnSystemBreakpoint;
    callbacks.userdata = &resume;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);
    resume.engine = engine;

    DbgLaunchSpec spec{};
    spec.path = MACHBUG_TESTS_TARGETS_DIR "/exit_code_42";

    REQUIRE(engine->Start(engine->impl, &spec) == DbgStatus_Ok);
    REQUIRE(engine->GetPid(engine->impl) > 0);

    REQUIRE(engine->Continue(engine->impl) == DbgStatus_TargetExited);
    REQUIRE(engine->StepInto(engine->impl) == DbgStatus_TargetExited);
    REQUIRE(engine->Pause(engine->impl) == DbgStatus_TargetExited);
    REQUIRE(engine->Stop(engine->impl) == DbgStatus_TargetExited);

    MachBugDestroy(engine);
}

TEST_CASE("Start rejects a launch spec with neither a path nor an attach pid")
{
    DbgEngineCallbacks callbacks{};
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    DbgLaunchSpec spec{};
    REQUIRE(engine->Start(engine->impl, &spec) == DbgStatus_InvalidArgument);
    REQUIRE(engine->GetPid(engine->impl) == 0);

    MachBugDestroy(engine);
}

namespace
{
    // Signals from the C callbacks below, used the same way MachBug::test::RecordingDebugger
    // (tests/TestHarness.h) uses its own recorded event timeline -- except here nothing has
    // direct access to MachBug::Debugger at all, only the DbgEngineCallbacks the vtable actually
    // invokes, which is the point: this is a test of the C boundary, not of the C++ class
    // underneath it.
    struct AttachHarness
    {
        std::atomic<bool> created{false};
        std::atomic<pid_t> createdPid{0};
        std::atomic<bool> startReturned{false};
        std::atomic<int> startResult{-1};
        std::mutex errorMutex;
        std::string lastError; // diagnostic only -- see onError below

        // The vtable leaves a target's first stop parked rather than resuming it automatically
        // (machbug_api.cpp's header comment) -- set once MachBugCreate() has returned so
        // onSystemBreakpoint below can call Continue() itself, the same way a real caller would.
        DbgEngine* engine = nullptr;
    };

    void onCreateProcess(pid_t pid, uint64_t /*entryPoint*/, void* userdata)
    {
        auto* h = static_cast<AttachHarness*>(userdata);
        h->createdPid.store(pid, std::memory_order_release);
        h->created.store(true, std::memory_order_release);
    }

    // This test wants "attach, then run freely" -- not interactive control -- so it resumes the
    // one stop every attach produces itself, exactly as a real caller reaching for the same
    // behavior would (see machbug_api.cpp's header comment: the vtable does not decide this on
    // the caller's behalf).
    void onSystemBreakpoint(void* userdata)
    {
        auto* h = static_cast<AttachHarness*>(userdata);
        if(h->engine)
            h->engine->Continue(h->engine->impl);
    }

    // Wired for diagnostics only: if Attach() fails, the assertions below would otherwise report
    // only "harness.created was never set", the same generic shape a bare boolean check on
    // MachBug::Debugger::Attach() itself would give a caller -- exactly the failure mode this
    // task's null-vs-stub decision (machbug_api.cpp's header comment) exists to avoid for the
    // memory/register/module/breakpoint entries. onError is the one place this vtable exposes
    // cbInternalError()'s named diagnostic at all, so a real regression here says why, not just
    // that it happened.
    void onError(const char* error, void* userdata)
    {
        auto* h = static_cast<AttachHarness*>(userdata);
        std::lock_guard<std::mutex> lock(h->errorMutex);
        h->lastError = error ? error : "(null)";
    }
}

// Step 2's second test, adapted from the brief: attach to a process this engine did not launch,
// via spec.attachPid, and prove the attach path tears down as cleanly as the launch path already
// does (exception_loop.cpp's "Stop() kills a genuinely running target..." is this test's model --
// same reasoning, same kill(pid, 0) kernel-verified check, applied to Attach() instead of Init()).
TEST_CASE("attach takes control of a process machdbg did not launch, and Stop tears it down cleanly")
{
    // Spawned directly with posix_spawn, not through the engine at all -- MachBugCreate()'s
    // Debugger has no idea this pid exists until Start() below attaches to it, which is the
    // entire point: this is "a process machdbg did not launch" (task brief), not merely a second
    // Debugger object launching the same binary.
    const std::string path = FIXTURE("run_endlessly");
    std::vector<char*> argv{const_cast<char*>(path.c_str()), nullptr};

    pid_t targetPid = 0;
    REQUIRE(posix_spawn(&targetPid, path.c_str(), nullptr, nullptr, argv.data(), environ) == 0);
    REQUIRE(targetPid > 0);

    AttachHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onCreateProcess = onCreateProcess;
    callbacks.onSystemBreakpoint = onSystemBreakpoint;
    callbacks.onError = onError;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);
    harness.engine = engine;

    DbgLaunchSpec spec{};
    spec.attachPid = targetPid;

    // Start() blocks for the life of the target (machbug_api.h), and run_endlessly never exits
    // on its own -- exactly why this needs its own thread, the same shape
    // MachBug::test::RecordingDebugger::StartOnThread() gives the launch-path tests in
    // exception_loop.cpp.
    //
    // Every REQUIRE that can fail is deliberately deferred until after this thread is joined or
    // detached, below -- a REQUIRE failing here would throw and unwind the stack through
    // `startThread` while it may still be joinable, and std::thread's destructor calls
    // std::terminate() on a joinable thread rather than quietly cleaning it up.
    std::thread startThread([&] {
        const DbgStatus result = engine->Start(engine->impl, &spec);
        harness.startResult.store(static_cast<int>(result), std::memory_order_release);
        harness.startReturned.store(true, std::memory_order_release);
    });

    // Wait for whichever comes first: onCreateProcess (attach succeeded, ports installed, about
    // to resume -- Debugger::cbCreateProcessEvent's comment) or Start() already having returned
    // (a fast failure, e.g. a denied task_for_pid). Either way there is nothing more to wait for.
    for(int i = 0; i < 500 &&
            !harness.created.load(std::memory_order_acquire) &&
            !harness.startReturned.load(std::memory_order_acquire);
        ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    const bool created = harness.created.load(std::memory_order_acquire);
    const pid_t createdPid = harness.createdPid.load(std::memory_order_acquire);

    if(created)
    {
        // A moment for the SIGCONT-turned-exception to actually arrive, for onSystemBreakpoint
        // (above) to call Continue() in response, and for run_endlessly to resume running freely
        // -- same margin exception_loop.cpp's own Stop()-while-running test gives a launched
        // target, so Stop() below exercises a live target, not one still parked at its first
        // stop or mid-resume.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    // Stop() is harmless (and idempotent) to call here regardless of `created`: if attach never
    // got that far, Start() has either already returned (Stop() then reports DbgStatus_NotAttached,
    // ignored below) or is stuck somewhere Stop() cannot reach -- either way, the bounded join
    // that follows is what actually detects and fails on a hang, not this call.
    engine->Stop(engine->impl);

    // Bounded, not a bare join(): the bug this guards against -- Process::DetachAndKill() not
    // running, or running against a pid still routing SIGKILL through an unserviced exception
    // port -- hangs this thread forever if it regresses. A timed poll turns that into a clean,
    // fast FAILED instead of a hung test binary; detaching (rather than joining) an abandoned
    // thread leaks it harmlessly for the rest of the process's life instead of blocking it.
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool finished = harness.startReturned.load(std::memory_order_acquire);
    if(finished)
        startThread.join();
    else
        startThread.detach();

    const DbgStatus startResult =
        static_cast<DbgStatus>(harness.startResult.load(std::memory_order_acquire));
    std::string diagnosticError;
    {
        std::lock_guard<std::mutex> lock(harness.errorMutex);
        diagnosticError = harness.lastError;
    }

    // From here on `startThread` is safely joined or detached: every REQUIRE below is free to
    // fail without risking std::terminate on its destructor.
    INFO("diagnostic the engine reported, if any: " << diagnosticError);
    REQUIRE(finished);
    REQUIRE(created);
    REQUIRE(createdPid == targetPid);
    REQUIRE(engine->GetPid(engine->impl) == targetPid);
    REQUIRE(startResult == DbgStatus_Ok);

    // The strongest confirmation available that the attached process is actually gone, not
    // merely that this engine's own bookkeeping believes so -- ask the kernel directly, exactly
    // as exception_loop.cpp's launch-path Stop() test does. kill(pid, 0) sends no signal but
    // still fails with ESRCH once pid no longer names a live process.
    errno = 0;
    const int rc = kill(targetPid, 0);
    INFO("kill(targetPid, 0) returned " << rc << ", errno " << errno << " (" << strerror(errno)
         << ") -- expected -1/ESRCH for a pid that is actually dead");
    REQUIRE(rc == -1);
    REQUIRE(errno == ESRCH);

    MachBugDestroy(engine);
}
