#pragma once

#include <capstone/capstone.h>

// The one place either spelling of the AArch64 architecture constant appears.
//
// Capstone 6 renamed CS_ARCH_ARM64 to CS_ARCH_AARCH64 (spec section 7, which says to confirm
// against the installed header rather than assume). MEASURED here: Homebrew ships 5.0.9 and
// therefore still has the old name -- so the rename has NOT happened at the version this
// machine builds against, and a port that wrote either spelling directly would break on
// whichever machine has the other one.
//
// That matters because Capstone comes from Homebrew by decision (the milestone 6 plan's
// decision 1), so its version is not this project's to pin. Nothing outside this header names
// CS_ARCH_ARM64 or CS_ARCH_AARCH64.
#if defined(CS_ARCH_AARCH64)
#define MACHDBG_CS_ARCH_ARM64 CS_ARCH_AARCH64
#else
#define MACHDBG_CS_ARCH_ARM64 CS_ARCH_ARM64
#endif
