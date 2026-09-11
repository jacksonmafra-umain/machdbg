// Milestone 6 task 1: that Capstone is linked, and that the port can open both architectures
// without naming either spelling of the AArch64 constant.
//
// This is the test that fails on a machine whose Capstone is too old, too new or absent --
// which matters here more than it usually would, because Capstone comes from Homebrew by
// decision and its version is therefore not this project's to pin.
#include <catch2/catch_test_macros.hpp>

#include <CapstoneVersion.h>

#include <string>

TEST_CASE("Capstone supports both of this port's architectures")
{
    REQUIRE(cs_support(MACHDBG_CS_ARCH_ARM64));
    REQUIRE(cs_support(CS_ARCH_X86));
}

TEST_CASE("a handle opens and closes for each architecture")
{
    csh arm64 = 0;
    REQUIRE(cs_open(MACHDBG_CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &arm64) == CS_ERR_OK);
    // Detail is what the token stream is built from (spec section 7); a handle opened without
    // it decodes text and nothing else, which is the design this milestone rejected.
    REQUIRE(cs_option(arm64, CS_OPT_DETAIL, CS_OPT_ON) == CS_ERR_OK);
    REQUIRE(cs_close(&arm64) == CS_ERR_OK);

    csh x86 = 0;
    REQUIRE(cs_open(CS_ARCH_X86, CS_MODE_64, &x86) == CS_ERR_OK);
    REQUIRE(cs_option(x86, CS_OPT_DETAIL, CS_OPT_ON) == CS_ERR_OK);
    REQUIRE(cs_close(&x86) == CS_ERR_OK);
}

TEST_CASE("the version is one this port has been built against")
{
    int major = 0;
    int minor = 0;
    cs_version(&major, &minor);
    INFO("Capstone " << major << "." << minor);
    // Five or newer. The spec notes the architecture constant was renamed in 6, which
    // CapstoneVersion.h absorbs; below 5 the API itself differs and the shim would not be
    // enough.
    REQUIRE(major >= 5);
}
