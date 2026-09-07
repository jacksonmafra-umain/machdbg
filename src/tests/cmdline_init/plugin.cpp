#include <Windows.h>

#include <cstring>
#include <string>

#include "_plugins.h"
#include "bridgemain.h"


namespace
{
    int gPluginHandle = 0;

    duint resolveExport(const char* module, const char* name)
    {
        std::string expr = std::string(module) + ":" + name;
        return DbgValFromString(expr.c_str());
    }

    template <typename T>
    bool readValue(const char* module, const char* name, T & value)
    {
        auto addr = resolveExport(module, name);
        return addr != 0 && DbgMemRead(addr, &value, sizeof(value));
    }

    template <size_t Count>
    bool readWide(const char* module, const char* name, wchar_t (&buffer)[Count])
    {
        memset(buffer, 0, sizeof(buffer));
        auto addr = resolveExport(module, name);
        return addr != 0 && DbgMemRead(addr, buffer, sizeof(buffer));
    }

    bool contains(const wchar_t* haystack, const wchar_t* needle)
    {
        return haystack && needle && wcsstr(haystack, needle) != nullptr;
    }

    bool cbCmdlineAssert(int, char**)
    {
        LONG ready = 0;
        int crtArgc = 0;
        int winArgc = 0;
        wchar_t commandLine[512];
        wchar_t crtArgv1[128];
        wchar_t crtArgv2[128];
        wchar_t winArgv1[128];
        wchar_t winArgv2[128];

        const char* module = "cmdline_init";
        if(!_plugin_testassert(readValue(module, "ObservedReady", ready), "failed to read ObservedReady"))
            return false;
        if(!_plugin_testassert(readValue(module, "ObservedCrtArgc", crtArgc), "failed to read ObservedCrtArgc"))
            return false;
        if(!_plugin_testassert(readValue(module, "ObservedWinArgc", winArgc), "failed to read ObservedWinArgc"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCommandLine", commandLine), "failed to read ObservedCommandLine"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCrtArgv1", crtArgv1), "failed to read ObservedCrtArgv1"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCrtArgv2", crtArgv2), "failed to read ObservedCrtArgv2"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedWinArgv1", winArgv1), "failed to read ObservedWinArgv1"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedWinArgv2", winArgv2), "failed to read ObservedWinArgv2"))
            return false;

        if(!_plugin_testassert(ready == 1, "target did not reach the observation point"))
            return false;
        if(!_plugin_testassert(crtArgc == 3, "expected CRT argc=3, got %d", crtArgc))
            return false;
        if(!_plugin_testassert(winArgc == 3, "expected WIN argc=3, got %d", winArgc))
            return false;
        if(!_plugin_testassert(wcscmp(crtArgv1, L"alpha") == 0, "expected CRT argv[1]='alpha'"))
            return false;
        if(!_plugin_testassert(wcscmp(crtArgv2, L"beta") == 0, "expected CRT argv[2]='beta'"))
            return false;
        if(!_plugin_testassert(wcscmp(winArgv1, L"alpha") == 0, "expected WIN argv[1]='alpha'"))
            return false;
        if(!_plugin_testassert(wcscmp(winArgv2, L"beta") == 0, "expected WIN argv[2]='beta'"))
            return false;
        if(!_plugin_testassert(contains(commandLine, L"alpha"), "expected command line to contain 'alpha'"))
            return false;
        return _plugin_testassert(contains(commandLine, L"beta"), "expected command line to contain 'beta'");
    }

    bool cbCmdlineDbOverrideAssert(int, char**)
    {
        LONG ready = 0;
        int crtArgc = 0;
        int winArgc = 0;
        wchar_t commandLine[512];
        wchar_t crtArgv1[128];
        wchar_t crtArgv2[128];
        wchar_t winArgv1[128];
        wchar_t winArgv2[128];

        const char* module = "cmdline_init";
        if(!_plugin_testassert(readValue(module, "ObservedReady", ready), "failed to read ObservedReady"))
            return false;
        if(!_plugin_testassert(readValue(module, "ObservedCrtArgc", crtArgc), "failed to read ObservedCrtArgc"))
            return false;
        if(!_plugin_testassert(readValue(module, "ObservedWinArgc", winArgc), "failed to read ObservedWinArgc"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCommandLine", commandLine), "failed to read ObservedCommandLine"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCrtArgv1", crtArgv1), "failed to read ObservedCrtArgv1"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedCrtArgv2", crtArgv2), "failed to read ObservedCrtArgv2"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedWinArgv1", winArgv1), "failed to read ObservedWinArgv1"))
            return false;
        if(!_plugin_testassert(readWide(module, "ObservedWinArgv2", winArgv2), "failed to read ObservedWinArgv2"))
            return false;

        if(!_plugin_testassert(ready == 1, "target did not reach the observation point"))
            return false;
        if(!_plugin_testassert(crtArgc == 3, "expected CRT argc=3, got %d", crtArgc))
            return false;
        if(!_plugin_testassert(winArgc == 3, "expected WIN argc=3, got %d", winArgc))
            return false;
        // Explicit args must win over the DB-saved "alpha"/"beta" from the first run
        if(!_plugin_testassert(wcscmp(crtArgv1, L"gamma") == 0, "expected CRT argv[1]='gamma' (DB-saved 'alpha' must not override explicit command line)"))
            return false;
        if(!_plugin_testassert(wcscmp(crtArgv2, L"delta") == 0, "expected CRT argv[2]='delta' (DB-saved 'beta' must not override explicit command line)"))
            return false;
        if(!_plugin_testassert(wcscmp(winArgv1, L"gamma") == 0, "expected WIN argv[1]='gamma'"))
            return false;
        if(!_plugin_testassert(wcscmp(winArgv2, L"delta") == 0, "expected WIN argv[2]='delta'"))
            return false;
        if(!_plugin_testassert(contains(commandLine, L"gamma"), "expected command line to contain 'gamma'"))
            return false;
        if(!_plugin_testassert(contains(commandLine, L"delta"), "expected command line to contain 'delta'"))
            return false;
        if(!_plugin_testassert(!contains(commandLine, L"alpha"), "command line must not contain DB-saved argument 'alpha'"))
            return false;
        return _plugin_testassert(!contains(commandLine, L"beta"), "command line must not contain DB-saved argument 'beta'");
    }
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* initStruct)
{
    initStruct->pluginVersion = 1;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strncpy_s(initStruct->pluginName, sizeof(initStruct->pluginName), X64DBG_TEST_NAME, _TRUNCATE);
    gPluginHandle = initStruct->pluginHandle;
    _plugin_registercommand(gPluginHandle, "cmdlineassert", cbCmdlineAssert, true);
    _plugin_registercommand(gPluginHandle, "cmdlinedboverrideassert", cbCmdlineDbOverrideAssert, true);
    return true;
}

extern "C" __declspec(dllexport) void plugstop()
{
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT*)
{
}
