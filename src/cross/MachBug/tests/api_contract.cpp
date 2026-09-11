#include <catch2/catch_test_macros.hpp>

#include <MachBug/api/machbug_api.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdio>
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

// The entry list this test walks is deliberately the *whole* memory/register/module/breakpoint
// surface, not only the unimplemented part: what it holds is that every entry is callable and
// says something honest about itself. Which of them are stubs shrinks each milestone -- registers
// and memory left that set in milestone 3 -- and the assertions below move with it.
TEST_CASE("every vtable entry is callable and answers honestly about itself")
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

    // Registers and memory are implemented as of milestone 3, so they no longer answer
    // NotSupported: with nothing launched they answer NotAttached, which is a different and more
    // useful sentence -- "this engine can do that, but not yet, because there is no target".
    // Asserting the *old* status here would have this test fail the day the feature landed, and
    // asserting nothing would let a regression to a stub go unnoticed.
    DbgRegisters regs{};
    REQUIRE(vtStatus(engine->GetRegisters(engine->impl, 0, &regs)) == DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->SetRegister(engine->impl, 0, "rip", 0)) == DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->MemRead(engine->impl, 0, &dummy, sizeof(dummy))) == DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->MemWrite(engine->impl, 0, &dummy, sizeof(dummy))) == DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->MemFindBaseAddr(engine->impl, 0, &dummy, &dummy)) == DbgStatus_NotAttached);
    REQUIRE_FALSE(engine->MemIsCodePtr(engine->impl, 0));
    REQUIRE_FALSE(engine->MemIsValidPtr(engine->impl, 0));

    // The descriptor table is the one entry that answers with no target at all, because it is
    // data rather than a question about a process.
    uint32_t count = 123;
    REQUIRE(engine->GetRegisterDescs(engine->impl, DbgArch_Arm64, &count) != nullptr);
    REQUIRE(count == 34);

    // ...and an architecture nothing implements still gets nothing, so the table cannot hand a
    // caller the wrong register file.
    count = 123;
    REQUIRE(engine->GetRegisterDescs(engine->impl, DbgArch_I386, &count) == nullptr);
    REQUIRE(count == 0);

    count = 123;
    REQUIRE(vtStatus(engine->MemEnumRegions(engine->impl, nullptr, 0, &count)) == DbgStatus_NotAttached);
    REQUIRE(count == 0);

    REQUIRE(vtStatus(engine->ModBaseFromAddr(engine->impl, 0, &dummy)) == DbgStatus_NotSupported);
    char buf[8];
    REQUIRE(vtStatus(engine->ModNameFromAddr(engine->impl, 0, buf, sizeof(buf))) == DbgStatus_NotSupported);

    // NotAttached rather than NotSupported since milestone 4 task 7: these are implemented now,
    // and what is missing is a target rather than the feature.
    REQUIRE(vtStatus(engine->SetBreakpoint(engine->impl, DbgBreakpointKind_Software, 0, 1)) ==
            DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->DeleteBreakpoint(engine->impl, 0)) == DbgStatus_NotAttached);
    REQUIRE(vtStatus(engine->SetBreakpointEnabled(engine->impl, 0, false)) ==
            DbgStatus_NotAttached);
    REQUIRE_FALSE(engine->IsBreakpointEffective(engine->impl, 0));

    // The slot count answers with no target at all: it describes the machine, not the process,
    // and a UI drawing a breakpoint panel needs it before anything is launched.
    REQUIRE(engine->GetHwBreakpointSlots(engine->impl) >= 4);
    REQUIRE(engine->GetHwBreakpointSlots(engine->impl) < 16);

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

namespace
{
    // Records that onSystemBreakpoint fired and, deliberately, does nothing else -- unlike
    // continueOnSystemBreakpoint above, this callback never calls Continue(). That silence is
    // the point: it is what lets the test below observe the target still parked, not resumed
    // for it by anything in this file or in machbug_api.cpp.
    struct ParkingProbe
    {
        std::atomic<bool> hit{false};
    };

    void recordSystemBreakpoint(void* userdata)
    {
        static_cast<ParkingProbe*>(userdata)->hit.store(true, std::memory_order_release);
    }
}

// Commits the property Decision 2 (machbug_api.cpp's header comment) rests on: the vtable leaves
// a target's first stop parked until a caller calls Continue(), rather than deciding on the
// caller's behalf that the answer is always "run" (which an earlier version of this PR did, and
// a reviewer caught precisely because nothing in this suite would have noticed a regression back
// to it -- every other test here wires onSystemBreakpoint straight to Continue()). Without this
// test, a future change that reinstated auto-resume would pass the entire suite.
TEST_CASE("the target stays parked at its first stop until Continue() is called")
{
    ParkingProbe probe;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = recordSystemBreakpoint;
    callbacks.userdata = &probe;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    DbgLaunchSpec spec{};
    spec.path = MACHBUG_TESTS_TARGETS_DIR "/exit_code_42";

    // Start() blocks for the target's whole life and nothing here ever resumes it until this
    // test says so explicitly, below -- exactly why this needs its own thread, the same shape
    // the attach test further down uses.
    std::atomic<bool> startReturned{false};
    std::atomic<int> startResult{-1};
    std::thread startThread([&] {
        const DbgStatus result = engine->Start(engine->impl, &spec);
        startResult.store(static_cast<int>(result), std::memory_order_release);
        startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !probe.hit.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(probe.hit.load(std::memory_order_acquire));

    const pid_t pid = engine->GetPid(engine->impl);
    REQUIRE(pid > 0);

    // Two full seconds parked with no Continue() call -- long enough that a regression back to
    // auto-resume (or any other path that lets the target run without being asked) would already
    // have let exit_code_42 finish and exit by now. Neither observable should have moved:
    // Start() must not have returned, and the process itself must still exist.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    REQUIRE_FALSE(startReturned.load(std::memory_order_acquire));

    errno = 0;
    const int aliveCheck = kill(pid, 0);
    INFO("kill(pid, 0) returned " << aliveCheck << ", errno " << errno << " (" << strerror(errno)
         << ") -- expected 0: the target should still be alive, parked at its first stop");
    REQUIRE(aliveCheck == 0);

    // Now resume, and confirm it actually runs to completion -- the other half of the property:
    // parking is not a hang, Continue() genuinely lets it go.
    REQUIRE(engine->Continue(engine->impl) == DbgStatus_Ok);

    bool finished = false;
    for(int i = 0; i < 500 && !startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    finished = startReturned.load(std::memory_order_acquire);
    if(finished)
        startThread.join();
    else
        startThread.detach(); // see the attach test below for why detach, not a bare join()

    REQUIRE(finished);
    REQUIRE(static_cast<DbgStatus>(startResult.load(std::memory_order_acquire)) == DbgStatus_Ok);

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
        std::atomic<bool> resumed{false};
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

    // The engine reports this once the reply that answers a stop has actually been sent, which
    // is what resumes the target thread (cbResumed() in core/Debugger.h) -- here, the reply the
    // Continue() above decided on. It is the signal that the attached target is running for
    // real, which nothing else in this vtable can tell a caller.
    void onResumed(void* userdata)
    {
        auto* h = static_cast<AttachHarness*>(userdata);
        h->resumed.store(true, std::memory_order_release);
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
    callbacks.onResumed = onResumed;
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

    // Waits for the SIGCONT-turned-exception to arrive, for onSystemBreakpoint (above) to answer
    // it with Continue(), and for that reply to have gone out -- onResumed is the engine's report
    // that the last of those has happened and run_endlessly is running freely again, so Stop()
    // below exercises a live target rather than one still parked at its first stop or mid-resume.
    // The same wait exception_loop.cpp's Stop()-while-running test makes on EventType::Resumed,
    // through the C vtable instead of the harness. Unbounded polling is not an option here: this
    // whole block runs while `startThread` is still joinable, so it records a bool for the
    // REQUIRE that follows the join rather than asserting in place.
    for(int i = 0; created && i < 500 && !harness.resumed.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool resumed = harness.resumed.load(std::memory_order_acquire);
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
    REQUIRE(resumed);
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

// Milestone 3 task 5: the same registers and memory the engine's own tests cover, reached only
// through the C contract a Qt view will actually call -- no MachBug::Debugger, no MachBug::arch.
// The stop is reached with a callback that deliberately does *not* answer it, so the target stays
// parked while every assertion runs.
namespace
{
    struct StopHarness
    {
        std::atomic<bool> stopped{false};
        std::atomic<bool> startReturned{false};
    };

    void onStopHarnessSystemBreakpoint(void* userdata)
    {
        static_cast<StopHarness*>(userdata)->stopped.store(true, std::memory_order_release);
    }
}

TEST_CASE("the vtable reports registers and memory for a stopped target")
{
    StopHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = onStopHarnessSystemBreakpoint;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    const std::string path = FIXTURE("run_endlessly");
    DbgLaunchSpec spec{};
    spec.path = path.c_str();

    std::thread startThread([&] {
        engine->Start(engine->impl, &spec);
        harness.startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !harness.stopped.load(std::memory_order_acquire) &&
                   !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool stopped = harness.stopped.load(std::memory_order_acquire);

    const DbgArch arch = engine->GetArch(engine->impl);
    uint32_t count = 0;
    const DbgRegisterDesc* descs = engine->GetRegisterDescs(engine->impl, arch, &count);
    const bool haveDescs = descs != nullptr && count > 0;

    DbgRegisters regs{};
    const DbgStatus registersResult = engine->GetRegisters(engine->impl, 0, &regs);

    // Reading the program counter through the descriptor table, not through the struct field:
    // this is the assertion that makes the table trustworthy for a view that only has the table.
    // The offset is relative to the active arm of the union (machbug_api.h), so the base is that
    // arm rather than &regs.
    uint64_t throughTable = 0;
    if(haveDescs)
    {
        const uint8_t* base = arch == DbgArch_Arm64
            ? reinterpret_cast<const uint8_t*>(&regs.arm64)
            : reinterpret_cast<const uint8_t*>(&regs.x86_64);
        for(uint32_t i = 0; i < count; ++i)
        {
            if((descs[i].flags & DbgRegisterFlag_ProgramCounter) != 0)
                std::memcpy(&throughTable, base + descs[i].offset, sizeof(throughTable));
        }
    }

    uint8_t byte = 0;
    const DbgStatus readResult = engine->MemRead(engine->impl, throughTable, &byte, 1);
    const bool isCode = engine->MemIsCodePtr(engine->impl, throughTable);
    const bool isValid = engine->MemIsValidPtr(engine->impl, throughTable);
    const bool nullIsValid = engine->MemIsValidPtr(engine->impl, 0x10);

    uint64_t base = 0;
    uint64_t size = 0;
    const DbgStatus baseResult = engine->MemFindBaseAddr(engine->impl, throughTable, &base, &size);

    DbgMemoryRegion regions[64]{};
    uint32_t regionCount = 0;
    const DbgStatus enumResult = engine->MemEnumRegions(engine->impl, regions, 64, &regionCount);

    // Torn down before the assertions, the same way the attach test above does it: Stop(), a
    // bounded wait, join if it finished and detach if it did not, so a regression in teardown is
    // a fast failure rather than a hung binary or a std::terminate from a joinable thread.
    engine->Stop(engine->impl);
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(harness.startReturned.load(std::memory_order_acquire))
        startThread.join();
    else
        startThread.detach();

    INFO("last engine diagnostic: " << DbgLastErrorString());
    REQUIRE(stopped);
    REQUIRE(haveDescs);
    REQUIRE(registersResult == DbgStatus_Ok);
    REQUIRE(regs.arch == arch);
    REQUIRE(throughTable > 0x1000);

    REQUIRE(readResult == DbgStatus_Ok);
    REQUIRE(isCode);
    REQUIRE(isValid);
    REQUIRE_FALSE(nullIsValid);

    REQUIRE(baseResult == DbgStatus_Ok);
    REQUIRE(base <= throughTable);
    REQUIRE(throughTable < base + size);

    REQUIRE(enumResult == DbgStatus_Ok);
    REQUIRE(regionCount > 0);
    REQUIRE(regions[0].size > 0);

    MachBugDestroy(engine);
}

TEST_CASE("the vtable refuses a register read with no stop to read from")
{
    DbgEngineCallbacks callbacks{};
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    // Never started: there is no target, let alone a stopped thread.
    DbgRegisters regs{};
    REQUIRE(engine->GetRegisters(engine->impl, 0, &regs) == DbgStatus_NotAttached);
    REQUIRE_FALSE(std::string(DbgLastErrorString()).empty());

    REQUIRE(engine->SetRegister(engine->impl, 0, "pc", 0) == DbgStatus_NotAttached);
    uint8_t byte = 0;
    REQUIRE(engine->MemRead(engine->impl, 0x1000, &byte, 1) == DbgStatus_NotAttached);

    // A table is data and answers regardless: a view can lay out its rows before a target exists.
    uint32_t count = 0;
    REQUIRE(engine->GetRegisterDescs(engine->impl, DbgArch_Arm64, &count) != nullptr);
    REQUIRE(count == 34);

    MachBugDestroy(engine);
}

// Milestone 4 task 7: the breakpoint entries of the same contract. Everything milestone 4 built
// was reachable only from C++ until these four were filled in, and a Qt view calls nothing else.
namespace
{
    struct BreakpointHarness
    {
        std::atomic<bool> stopped{false};
        std::atomic<bool> startReturned{false};
        std::atomic<int> hits{0};
        std::atomic<uint64_t> lastAddress{0};
    };

    void onBreakpointHarnessSystemBreakpoint(void* userdata)
    {
        static_cast<BreakpointHarness*>(userdata)->stopped.store(true, std::memory_order_release);
    }

    void onBreakpointHarnessBreakpoint(const uint64_t address, void* userdata)
    {
        auto* harness = static_cast<BreakpointHarness*>(userdata);
        harness->lastAddress.store(address, std::memory_order_release);
        harness->hits.fetch_add(1, std::memory_order_acq_rel);
    }
}

TEST_CASE("a breakpoint set through the vtable stops the target and reports its address")
{
    BreakpointHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = onBreakpointHarnessSystemBreakpoint;
    callbacks.onBreakpoint = onBreakpointHarnessBreakpoint;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    const std::string path = FIXTURE("known_function");
    DbgLaunchSpec spec{};
    spec.path = path.c_str();

    // The fixture publishes the address on stdout, so this process's stdout is redirected for
    // the whole run rather than only around the launch: Start() blocks for the target's life
    // here, unlike Debugger::Init(). Nothing is asserted while the redirect is in place --
    // Catch2's own output would land in the capture file and corrupt the line being read.
    char outPathTemplate[] = "/tmp/machbug_contract_breakpoint.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;

    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);
    std::fflush(stdout);
    const int redirected = dup2(outFd, STDOUT_FILENO);

    std::thread startThread([&] {
        engine->Start(engine->impl, &spec);
        harness.startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !harness.stopped.load(std::memory_order_acquire) &&
                   !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool stopped = harness.stopped.load(std::memory_order_acquire);

    // Resumed so the fixture reaches its printf, then paused again: a breakpoint is written into
    // a stopped target.
    engine->Continue(engine->impl);

    uint64_t functionAddress = 0;
    for(int i = 0; i < 500 && functionAddress == 0; ++i)
    {
        if(FILE* captured = std::fopen(outPath.c_str(), "r"))
        {
            unsigned long long printed = 0;
            if(std::fscanf(captured, "%llx", &printed) == 1)
                functionAddress = printed;
            std::fclose(captured);
        }
        if(functionAddress == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::fflush(stdout);
    const int restored = dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(outFd);
    unlink(outPath.c_str());

    engine->Pause(engine->impl);

    const uint32_t slots = engine->GetHwBreakpointSlots(engine->impl);
    const DbgStatus setResult =
        engine->SetBreakpoint(engine->impl, DbgBreakpointKind_Software, functionAddress, 0);
    const bool effectiveAfterSet = engine->IsBreakpointEffective(engine->impl, functionAddress);
    const std::string setDiagnostic = DbgLastErrorString();

    engine->Continue(engine->impl);
    for(int i = 0; i < 500 && harness.hits.load(std::memory_order_acquire) == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const int hits = harness.hits.load(std::memory_order_acquire);
    const uint64_t reported = harness.lastAddress.load(std::memory_order_acquire);

    // Disabled means the target stops carrying it while the caller keeps the breakpoint. The
    // round trip is what separates this entry from delete-and-set-again, which would be a
    // different breakpoint that happened to share an address.
    const DbgStatus disableResult =
        engine->SetBreakpointEnabled(engine->impl, functionAddress, false);
    const bool effectiveWhileDisabled =
        engine->IsBreakpointEffective(engine->impl, functionAddress);
    const DbgStatus enableResult =
        engine->SetBreakpointEnabled(engine->impl, functionAddress, true);
    const bool effectiveAfterEnable =
        engine->IsBreakpointEffective(engine->impl, functionAddress);

    const DbgStatus deleteResult = engine->DeleteBreakpoint(engine->impl, functionAddress);
    const bool effectiveAfterDelete = engine->IsBreakpointEffective(engine->impl, functionAddress);
    const DbgStatus deleteAgain = engine->DeleteBreakpoint(engine->impl, functionAddress);

    engine->Stop(engine->impl);
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(harness.startReturned.load(std::memory_order_acquire))
        startThread.join();
    else
        startThread.detach();

    INFO("redirect " << redirected << ", restore " << restored << ", fixture function at 0x"
         << std::hex << functionAddress << ", diagnostic: " << setDiagnostic);
    REQUIRE(stopped);
    REQUIRE(functionAddress != 0);
    REQUIRE(setResult == DbgStatus_Ok);
    REQUIRE(effectiveAfterSet);
    REQUIRE(hits >= 1);
    REQUIRE(reported == functionAddress);

    REQUIRE(disableResult == DbgStatus_Ok);
    REQUIRE_FALSE(effectiveWhileDisabled);
    REQUIRE(enableResult == DbgStatus_Ok);
    REQUIRE(effectiveAfterEnable);

    REQUIRE(deleteResult == DbgStatus_Ok);
    // Effective means the target is carrying it, so a deleted breakpoint is not effective and a
    // second delete is an argument error rather than a silent success.
    REQUIRE_FALSE(effectiveAfterDelete);
    REQUIRE(deleteAgain == DbgStatus_InvalidArgument);

    // The machine's slots, not the sixteen the kernel's arrays are wide.
    INFO("hardware execution slots reported: " << slots);
    REQUIRE(slots >= 4);
    REQUIRE(slots < 16);

    MachBugDestroy(engine);
}

TEST_CASE("the breakpoint entries refuse a target that is not there, by name")
{
    DbgEngineCallbacks callbacks{};
    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    REQUIRE(engine->SetBreakpoint(engine->impl, DbgBreakpointKind_Software, 0x1000, 0) ==
            DbgStatus_NotAttached);
    REQUIRE_FALSE(std::string(DbgLastErrorString()).empty());
    REQUIRE(engine->DeleteBreakpoint(engine->impl, 0x1000) == DbgStatus_NotAttached);
    REQUIRE_FALSE(engine->IsBreakpointEffective(engine->impl, 0x1000));

    MachBugDestroy(engine);
}

TEST_CASE("the memory map names the module a region belongs to")
{
    BreakpointHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = onBreakpointHarnessSystemBreakpoint;
    callbacks.onBreakpoint = onBreakpointHarnessBreakpoint;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    const std::string path = FIXTURE("known_function");
    DbgLaunchSpec spec{};
    spec.path = path.c_str();

    // A breakpoint stop, not a Pause, and for two reasons that both matter here. A Pause is a
    // task_suspend with no exception behind it, so there is no stopped thread and threadId 0
    // names nothing -- registers cannot be read from it. And this stop is after the target has
    // reached its own code, so dyld has published the image list a region needs to be
    // attributed to a module at all.
    char outPathTemplate[] = "/tmp/machbug_contract_map.XXXXXX";
    const int outFd = mkstemp(outPathTemplate);
    REQUIRE(outFd != -1);
    const std::string outPath = outPathTemplate;
    const int savedStdout = dup(STDOUT_FILENO);
    REQUIRE(savedStdout != -1);
    std::fflush(stdout);
    dup2(outFd, STDOUT_FILENO);

    std::thread startThread([&] {
        engine->Start(engine->impl, &spec);
        harness.startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !harness.stopped.load(std::memory_order_acquire) &&
                   !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool stopped = harness.stopped.load(std::memory_order_acquire);

    engine->Continue(engine->impl);

    uint64_t functionAddress = 0;
    for(int i = 0; i < 500 && functionAddress == 0; ++i)
    {
        if(FILE* captured = std::fopen(outPath.c_str(), "r"))
        {
            unsigned long long printed = 0;
            if(std::fscanf(captured, "%llx", &printed) == 1)
                functionAddress = printed;
            std::fclose(captured);
        }
        if(functionAddress == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::fflush(stdout);
    dup2(savedStdout, STDOUT_FILENO);
    close(savedStdout);
    close(outFd);
    unlink(outPath.c_str());

    engine->Pause(engine->impl);
    const DbgStatus setResult =
        engine->SetBreakpoint(engine->impl, DbgBreakpointKind_Software, functionAddress, 0);
    engine->Continue(engine->impl);
    for(int i = 0; i < 500 && harness.hits.load(std::memory_order_acquire) == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool hit = harness.hits.load(std::memory_order_acquire) > 0;

    DbgRegisters regs{};
    const DbgStatus registersResult = engine->GetRegisters(engine->impl, 0, &regs);
    const DbgArch arch = engine->GetArch(engine->impl);
    const uint64_t pc = arch == DbgArch_Arm64 ? regs.arm64.pc : regs.x86_64.rip;

    std::vector<DbgMemoryRegion> regions(512);
    uint32_t count = 0;
    const DbgStatus enumResult = engine->MemEnumRegions(engine->impl, regions.data(),
                                                        static_cast<uint32_t>(regions.size()),
                                                        &count);

    const DbgMemoryRegion* holdingPc = nullptr;
    bool everyTagNamed = true;
    for(uint32_t i = 0; i < count; ++i)
    {
        if(regions[i].tagName == nullptr || regions[i].tagName[0] == '\0')
            everyTagNamed = false;
        if(pc >= regions[i].base && pc < regions[i].base + regions[i].size)
            holdingPc = &regions[i];
    }

    engine->Stop(engine->impl);
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(harness.startReturned.load(std::memory_order_acquire))
        startThread.join();
    else
        startThread.detach();

    INFO("last engine diagnostic: " << DbgLastErrorString());
    REQUIRE(stopped);
    REQUIRE(functionAddress != 0);
    REQUIRE(setResult == DbgStatus_Ok);
    REQUIRE(hit);
    REQUIRE(registersResult == DbgStatus_Ok);
    REQUIRE(enumResult == DbgStatus_Ok);
    REQUIRE(count > 0);
    REQUIRE(everyTagNamed);

    REQUIRE(holdingPc != nullptr);
    INFO("the program counter is in a region at 0x" << std::hex << holdingPc->base
         << " tagged " << holdingPc->tagName << ", module base 0x" << holdingPc->moduleBase);
    // The region the target is executing in belongs to a module, and the engine says which. Its
    // tag is 0 -- measured -- so an implementation that read the module out of the tag would
    // answer zero here and look like it had simply found nothing.
    REQUIRE(holdingPc->moduleBase != 0);
    REQUIRE(holdingPc->moduleBase <= pc);

    MachBugDestroy(engine);
}

TEST_CASE("the vtable enumerates threads with names, states and program counters")
{
    BreakpointHarness harness;
    DbgEngineCallbacks callbacks{};
    callbacks.onSystemBreakpoint = onBreakpointHarnessSystemBreakpoint;
    callbacks.userdata = &harness;

    DbgEngine* engine = MachBugCreate(&callbacks);
    REQUIRE(engine != nullptr);

    const std::string path = FIXTURE("multi_threaded");
    DbgLaunchSpec spec{};
    spec.path = path.c_str();

    std::thread startThread([&] {
        engine->Start(engine->impl, &spec);
        harness.startReturned.store(true, std::memory_order_release);
    });

    for(int i = 0; i < 500 && !harness.stopped.load(std::memory_order_acquire) &&
                   !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool stopped = harness.stopped.load(std::memory_order_acquire);

    // At the first stop the fixture has not spawned anything yet, so this is the one thread it
    // was born with -- and the one the stop is on, which is the flag under test.
    DbgThread atFirstStop[8]{};
    uint32_t firstCount = 0;
    const DbgStatus firstResult = engine->ThreadEnum(engine->impl, atFirstStop, 8, &firstCount);
    const bool firstIsStopped = firstCount == 1 && atFirstStop[0].isStoppedThread;
    const uint64_t firstPc = firstCount == 1 ? atFirstStop[0].pc : 0;

    engine->Continue(engine->impl);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    engine->Pause(engine->impl);

    DbgThread threads[16]{};
    uint32_t count = 0;
    const DbgStatus result = engine->ThreadEnum(engine->impl, threads, 16, &count);

    std::vector<std::string> names;
    bool everyStateNamed = true;
    bool everyPcReadable = true;
    for(uint32_t i = 0; i < count; ++i)
    {
        if(threads[i].runStateName == nullptr || threads[i].runStateName[0] == '\0')
            everyStateNamed = false;
        if(threads[i].pc <= 0x1000)
            everyPcReadable = false;
        if(threads[i].name[0] != '\0')
            names.emplace_back(threads[i].name);
    }

    engine->Stop(engine->impl);
    for(int i = 0; i < 500 && !harness.startReturned.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if(harness.startReturned.load(std::memory_order_acquire))
        startThread.join();
    else
        startThread.detach();

    INFO("last engine diagnostic: " << DbgLastErrorString());
    REQUIRE(stopped);
    REQUIRE(firstResult == DbgStatus_Ok);
    REQUIRE(firstIsStopped);
    REQUIRE(firstPc > 0x1000);

    REQUIRE(result == DbgStatus_Ok);
    INFO(count << " threads, " << names.size() << " named");
    REQUIRE(count >= 3);
    REQUIRE(everyStateNamed);
    REQUIRE(everyPcReadable);
    REQUIRE(names.size() == 2);

    MachBugDestroy(engine);
}
