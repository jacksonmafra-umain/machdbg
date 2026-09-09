// Milestone 3 task 1: the thread identity a stop carries. Registers are per-thread, and this
// milestone deliberately has no thread enumeration (task_threads is milestone 5), so the port
// MIG hands handleException() is the only thread identity the engine has -- these tests are what
// hold it to being a real, checked one rather than a number that survived being stored.
#include <catch2/catch_test_macros.hpp>

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/thread_status.h>

#include <string>

#include <MachBug/core/Debugger.h>

#include "TestHarness.h"

#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>
#else
#include <mach/i386/thread_status.h>
#endif

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

using MachBug::test::EventType;
using MachBug::test::RecordingDebugger;

TEST_CASE("a stop names the thread that raised it, and threadId 0 resolves to it")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    const mach_port_t stopped = debugger.StoppedThread();
    REQUIRE(stopped != MACH_PORT_NULL);
    REQUIRE(debugger.ResolveThread(0) == stopped);

    // A threadId is the system thread id, not a port name -- milestone 4 changed that, because a
    // port name identifies a thread only until the next task_threads call mints new rights. The
    // id of the thread that is stopped resolves to a usable port for it; the port name itself,
    // handed back as an id, does not name any thread.
    thread_identifier_info_data_t identity{};
    mach_msg_type_number_t identityCount = THREAD_IDENTIFIER_INFO_COUNT;
    REQUIRE(thread_info(stopped, THREAD_IDENTIFIER_INFO,
                        reinterpret_cast<thread_info_t>(&identity),
                        &identityCount) == KERN_SUCCESS);
    REQUIRE(identity.thread_id != 0);
    REQUIRE(debugger.ResolveThread(identity.thread_id) != MACH_PORT_NULL);

    // The port is a real thread of the target, not a number that merely survived being stored.
#if defined(__arm64__) || defined(__aarch64__)
    arm_thread_state64_t state{};
    mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
    const kern_return_t kr = thread_get_state(stopped, ARM_THREAD_STATE64,
                                              reinterpret_cast<thread_state_t>(&state), &count);
#else
    x86_thread_state64_t state{};
    mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
    const kern_return_t kr = thread_get_state(stopped, x86_THREAD_STATE64,
                                              reinterpret_cast<thread_state_t>(&state), &count);
#endif
    INFO("thread_get_state returned " << kr << " (" << mach_error_string(kr) << ")");
    REQUIRE(kr == KERN_SUCCESS);

    // An id that names no thread of this target is not silently accepted.
    REQUIRE(debugger.ResolveThread(0xDEADBEEF) == MACH_PORT_NULL);
}

TEST_CASE("no thread is named while the target is running")
{
    RecordingDebugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    debugger.StartOnThread();
    REQUIRE(debugger.WaitFor(EventType::SystemBreakpoint));

    debugger.Continue();
    REQUIRE(debugger.WaitFor(EventType::Resumed));
    REQUIRE(debugger.StoppedThread() == MACH_PORT_NULL);
    REQUIRE(debugger.ResolveThread(0) == MACH_PORT_NULL);
}
