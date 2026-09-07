#pragma once

#include <Windows.h>
#include <memory>
#include <string>

struct HOST_STARTUP_OPTIONS
{
    std::wstring userDirectory;
};

inline HOST_STARTUP_OPTIONS ParseHostStartupOptions()
{
    HOST_STARTUP_OPTIONS options;

    int argc = 0;
    auto argv = std::unique_ptr<wchar_t* [], decltype(&::LocalFree)>(CommandLineToArgvW(GetCommandLineW(), &argc), ::LocalFree);
    if(!argv)
        return options;

    for(int i = 1; i < argc; i++)
    {
        std::wstring arg = argv[i];
        if(arg == L"--")
            break;

        std::wstring value;
        if(arg == L"-userdir")
        {
            if(i + 1 >= argc)
                continue;
            value = argv[++i];
        }
        else if(arg.rfind(L"-userdir=", 0) == 0)
        {
            value = arg.substr(9);
        }
        else
            continue;

        if(!value.empty())
            options.userDirectory = std::move(value);
    }

    return options;
}
