#include <MachBug/api/machbug_api.h>

/* Compiling this file is the test: the contract must be usable from plain C. */
DbgArch machbug_test_c_arch(void)
{
    DbgRegisters regs;
    regs.arch = DbgArch_Arm64;
    regs.arm64.pc = 0;
    return regs.arch;
}
