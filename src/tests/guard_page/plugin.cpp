#include <Windows.h>

#include <atomic>
#include <cstring>

#include "_plugins.h"


namespace
{
    int gPluginHandle = 0;
    std::atomic<unsigned int> gGuardPageFirstChanceCount{ 0 };
    std::atomic<unsigned int> gGuardPageSecondChanceCount{ 0 };
    std::atomic<unsigned long> gExitCode{ 0xFFFFFFFFu };

    void cbPlugin(CBTYPE cbType, void* callbackInfo)
    {
        if(cbType == CB_EXCEPTION)
        {
            auto info = static_cast<PLUG_CB_EXCEPTION*>(callbackInfo);
            if(info && info->Exception && info->Exception->ExceptionRecord.ExceptionCode == STATUS_GUARD_PAGE_VIOLATION)
            {
                if(info->Exception->dwFirstChance)
                    gGuardPageFirstChanceCount.fetch_add(1);
                else
                    gGuardPageSecondChanceCount.fetch_add(1);
            }
        }
        else if(cbType == CB_EXITPROCESS)
        {
            auto info = static_cast<PLUG_CB_EXITPROCESS*>(callbackInfo);
            if(info && info->ExitProcess)
                gExitCode = info->ExitProcess->dwExitCode;
        }
        else if(cbType == CB_INITDEBUG)
        {
            gGuardPageFirstChanceCount = 0;
            gGuardPageSecondChanceCount = 0;
            gExitCode = 0xFFFFFFFFu;
        }
    }

    bool cbGuardPageAssert(int, char**)
    {
        const auto first = gGuardPageFirstChanceCount.load();
        const auto second = gGuardPageSecondChanceCount.load();
        const auto exitCode = gExitCode.load();
        if(!_plugin_testassert(first == 1, "expected exactly 1 first-chance guard page exception, got %u", first))
            return false;
        return _plugin_testassert(second == 1, "expected exactly 1 second-chance guard page exception, got %u", second);
    }
}

extern "C" __declspec(dllexport) bool pluginit(PLUG_INITSTRUCT* initStruct)
{
    initStruct->pluginVersion = 1;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strncpy_s(initStruct->pluginName, sizeof(initStruct->pluginName), X64DBG_TEST_NAME, _TRUNCATE);
    gPluginHandle = initStruct->pluginHandle;
    _plugin_registercallback(gPluginHandle, CB_INITDEBUG, cbPlugin);
    _plugin_registercallback(gPluginHandle, CB_EXCEPTION, cbPlugin);
    _plugin_registercallback(gPluginHandle, CB_EXITPROCESS, cbPlugin);
    _plugin_registercommand(gPluginHandle, "guardpageassert", cbGuardPageAssert, false);
    return true;
}

extern "C" __declspec(dllexport) void plugstop()
{
}

extern "C" __declspec(dllexport) void plugsetup(PLUG_SETUPSTRUCT*)
{
}
