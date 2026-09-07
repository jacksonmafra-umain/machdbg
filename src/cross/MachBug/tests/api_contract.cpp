#include <catch2/catch_test_macros.hpp>

#include <MachBug/api/machbug_api.h>

#include <cstring>
#include <string>

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
