#pragma once

// Mirrors ElfBug/types/Global.h's role: shared type definitions used by more than one module
// that are not simple scalar typedefs (those live in types/MachBug.h). ElfBug's version holds
// its breakpoint infrastructure (BreakpointInfo, BreakpointCallback, the various breakpoint
// maps). MachBug has no breakpoint support yet -- that lands in milestone 3/4, alongside memory
// and register access -- so there is nothing to share across modules yet and this file is
// deliberately empty.
//
// It exists now, ahead of having content, so that the three-way split ElfBug uses --
// types/MachBug.h for scalars, types/Global.h for shared structures, core/ and process/ for
// behaviour -- is already in place for the tasks that populate it.

namespace MachBug
{
}
