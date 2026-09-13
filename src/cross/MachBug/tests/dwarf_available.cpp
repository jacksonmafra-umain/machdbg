// Milestone 7 task 1: libdwarf is linked, not merely installed. The engine's DWARF reader is
// worth nothing if the library is absent at link time and the failure only surfaces when a user
// opens a dSYM, so this is the first thing the suite asserts about it.
#include <catch2/catch_test_macros.hpp>

#include <libdwarf.h>

#include <string>

TEST_CASE("libdwarf is linked and reports its version")
{
    const char* version = dwarf_package_version();
    REQUIRE(version != nullptr);
    REQUIRE(std::string(version).size() > 0);
}

TEST_CASE("libdwarf refuses a path that is not an object file")
{
    // The failure path, checked here rather than discovered later: a debugger is pointed at
    // wrong files constantly -- a stripped binary, a dSYM that belongs to another build -- and
    // dwarf_init_path must answer rather than crash. DW_DLV_NO_ENTRY is what it gives for a
    // path that is not there at all.
    Dwarf_Debug dbg = nullptr;
    Dwarf_Error error = nullptr;
    const int result = dwarf_init_path("/nonexistent/not-an-object-file", nullptr, 0,
                                       DW_GROUPNUMBER_ANY, nullptr, nullptr, &dbg, &error);

    REQUIRE(result != DW_DLV_OK);
    if(result == DW_DLV_ERROR)
        dwarf_dealloc_error(dbg, error);
}
