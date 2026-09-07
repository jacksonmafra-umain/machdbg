#include <Windows.h>

#include <atomic>
#include <cstring>

#include "_plugins.h"


namespace
{
    int gPluginHandle = 0;
    std::atomic<unsigned int> gNormalBreakpointHits{ 0 };
    std::atomic<unsigned int> gDllBreakpointHits{ 0 };
    std::atomic<unsigned int> gDllMainHits{ 0 };

    void cbPlugin(CBTYPE cbType, void* callbackInfo)
    {
        if(cbType == CB_INITDEBUG)
        {
            gNormalBreakpointHits = 0;
            gDllBreakpointHits = 0;
            gDllMainHits = 0;
            return;
        }

        if(cbType != CB_BREAKPOINT)
            return;

        auto info = static_cast<PLUG_CB_BREAKPOINT*>(callbackInfo);
        if(info == nullptr || info->breakpoint == nullptr)
            return;

        if(info->breakpoint->type == bp_dll)
            gDllBreakpointHits.fetch_add(1);
        else if(info->breakpoint->type == bp_normal)
        {
            gNormalBreakpointHits.fetch_add(1);
            if(strstr(info->breakpoint->name, "DllMain (") == info->breakpoint->name)
                gDllMainHits.fetch_add(1);
        }
    }

    bool cbRandomDllAssert(int, char**)
    {
        const auto callbackCount = (unsigned int)DbgValFromString("$callback_count");
        const auto normalBreakpointHits = gNormalBreakpointHits.load();
        const auto dllBreakpointHits = gDllBreakpointHits.load();
        const auto dllMainHits = gDllMainHits.load();

        if(!_plugin_testassert(normalBreakpointHits >= 2, "expected at least 2 normal breakpoint callbacks, got %u", normalBreakpointHits))
            return false;
        if(!_plugin_testassert(callbackCount == 2, "expected script callback count 2, got %u", callbackCount))
            return false;
        if(!_plugin_testassert(dllBreakpointHits == 2, "expected dynamic DLL breakpoint hits 2, got %u", dllBreakpointHits))
            return false;
        return _plugin_testassert(dllMainHits == 2, "expected DllMain breakpoint hits 2, got %u", dllMainHits);
    }
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* initStruct)
{
    initStruct->pluginVersion = 1;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strncpy_s(initStruct->pluginName, sizeof(initStruct->pluginName), X64DBG_TEST_NAME, _TRUNCATE);
    gPluginHandle = initStruct->pluginHandle;
    _plugin_registercallback(gPluginHandle, CB_INITDEBUG, cbPlugin);
    _plugin_registercallback(gPluginHandle, CB_BREAKPOINT, cbPlugin);
    _plugin_registercommand(gPluginHandle, "randomdllassert", cbRandomDllAssert, true);
    return true;
}

extern "C" __declspec(dllexport) void plugstop()
{
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT*)
{
}
