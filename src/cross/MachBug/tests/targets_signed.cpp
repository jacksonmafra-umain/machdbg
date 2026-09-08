#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    // Reads the entitlements codesign reports for a file, or an empty string.
    std::string entitlementsOf(const std::string& path)
    {
        std::string command = "codesign --display --entitlements - \"" + path + "\" 2>&1";
        std::string output;
        if(FILE* pipe = popen(command.c_str(), "r"))
        {
            std::array<char, 512> buffer{};
            while(fgets(buffer.data(), int(buffer.size()), pipe) != nullptr)
                output += buffer.data();
            pclose(pipe);
        }
        return output;
    }
}

TEST_CASE("every test target is signed with get-task-allow")
{
    const std::string dir = MACHBUG_TESTS_TARGETS_DIR;
    for(const char* name : { "end_immediately", "exit_code_42", "hello_machbug",
                             "run_endlessly", "multi_threaded", "crash_bad_access" })
    {
        const std::string path = dir + "/" + name;
        INFO("target: " << path);
        REQUIRE(entitlementsOf(path).find("get-task-allow") != std::string::npos);
    }
}
