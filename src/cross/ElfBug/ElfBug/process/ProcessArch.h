#pragma once

#include <sys/types.h>
#include <ElfBug/types/ElfBug.h>

namespace ElfBug
{
    Arch detectArchFromElfPath(const char* path);
    Arch detectArchFromProcExe(pid_t pid);
}
