#include <catch2/catch_test_macros.hpp>
#include <MachBug/core/Debugger.h>

#include <string>

#define FIXTURE(name) (std::string(MACHBUG_TESTS_TARGETS_DIR "/") + (name))

TEST_CASE("a suspended launch yields a valid task port")
{
    MachBug::Debugger debugger;
    REQUIRE(debugger.Init(FIXTURE("run_endlessly").c_str()));
    REQUIRE(debugger.GetPid() > 0);
    REQUIRE(debugger.GetTaskPort() != MACH_PORT_NULL);
    // Start() is not running here, so there is no loop to Stop(); kill the suspended child.
    debugger.Terminate();
}

TEST_CASE("launching a path that does not exist fails cleanly")
{
    MachBug::Debugger debugger;
    REQUIRE_FALSE(debugger.Init("/nonexistent/machdbg-no-such-binary"));
    REQUIRE(debugger.GetPid() == 0);
}
