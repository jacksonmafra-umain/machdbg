#include <Windows.h>
#include <shellapi.h>

#include <stdlib.h>
#include <wchar.h>

extern "C"
{
    __declspec(dllexport) volatile LONG ObservedReady = 0;
    __declspec(dllexport) int ObservedCrtArgc = 0;
    __declspec(dllexport) int ObservedWinArgc = 0;
    __declspec(dllexport) wchar_t ObservedCommandLine[512] = L"";
    __declspec(dllexport) wchar_t ObservedCrtArgv0[128] = L"";
    __declspec(dllexport) wchar_t ObservedCrtArgv1[128] = L"";
    __declspec(dllexport) wchar_t ObservedCrtArgv2[128] = L"";
    __declspec(dllexport) wchar_t ObservedWinArgv0[128] = L"";
    __declspec(dllexport) wchar_t ObservedWinArgv1[128] = L"";
    __declspec(dllexport) wchar_t ObservedWinArgv2[128] = L"";
}

static void copyWideString(wchar_t* dest, size_t destCount, const wchar_t* src)
{
    if(destCount == 0)
        return;
    if(src == nullptr)
        src = L"";
    wcsncpy_s(dest, destCount, src, _TRUNCATE);
}

int wmain(int argc, wchar_t** argv)
{
    ObservedCrtArgc = argc;
    copyWideString(ObservedCommandLine, _countof(ObservedCommandLine), GetCommandLineW());
    if(argc > 0)
        copyWideString(ObservedCrtArgv0, _countof(ObservedCrtArgv0), argv[0]);
    if(argc > 1)
        copyWideString(ObservedCrtArgv1, _countof(ObservedCrtArgv1), argv[1]);
    if(argc > 2)
        copyWideString(ObservedCrtArgv2, _countof(ObservedCrtArgv2), argv[2]);

    int winArgc = 0;
    auto winArgv = CommandLineToArgvW(GetCommandLineW(), &winArgc);
    ObservedWinArgc = winArgc;
    if(winArgv)
    {
        if(winArgc > 0)
            copyWideString(ObservedWinArgv0, _countof(ObservedWinArgv0), winArgv[0]);
        if(winArgc > 1)
            copyWideString(ObservedWinArgv1, _countof(ObservedWinArgv1), winArgv[1]);
        if(winArgc > 2)
            copyWideString(ObservedWinArgv2, _countof(ObservedWinArgv2), winArgv[2]);
        LocalFree(winArgv);
    }

    InterlockedExchange(&ObservedReady, 1);
    __debugbreak();
    return 0;
}
