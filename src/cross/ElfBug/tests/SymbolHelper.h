#pragma once

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace ElfBug::test
{
    // ELF file offset of `symbol` in `path`, via `nm`.
    inline std::optional<ptr> ResolveFileOffset(const std::string & path, const std::string & symbol)
    {
        const std::string cmd = "nm --defined-only '" + path + "' 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if(!pipe) return std::nullopt;

        char line[512];
        std::optional<ptr> found;
        while(std::fgets(line, sizeof(line), pipe))
        {
            std::istringstream iss(line);
            unsigned long long addr = 0;
            char type = 0;
            std::string name;
            if((iss >> std::hex >> addr >> type >> name) && symbol == name)
            {
                found = static_cast<ptr>(addr);
                break;
            }
        }
        pclose(pipe);
        return found;
    }

    // Runtime-minus-file offset for `path`'s executable mapping in pid's /proc/maps.
    inline std::optional<ptr> GetExecLoadBias(pid_t pid, const std::string & path)
    {
        std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
        if(!maps) return std::nullopt;

        std::string line;
        while(std::getline(maps, line))
        {
            std::istringstream iss(line);
            unsigned long long low = 0, high = 0, offset = 0, inode = 0;
            char dash = 0;
            std::string perms, dev, pathname;

            if(!(iss >> std::hex >> low >> dash >> high >> perms >> offset >> dev >> std::dec >> inode))
                continue;

            std::getline(iss >> std::ws, pathname);
            if(perms.find('x') != std::string::npos && pathname == path)
                return low - offset;
        }
        return std::nullopt;
    }

    inline std::optional<ptr> ResolveRuntimeAddress(const std::string & path, const pid_t pid, const std::string & symbol)
    {
        const auto fileOffset = ResolveFileOffset(path, symbol);
        if(!fileOffset) return std::nullopt;
        const auto bias = GetExecLoadBias(pid, path);
        if(!bias) return std::nullopt;
        return *fileOffset + *bias;
    }
}
