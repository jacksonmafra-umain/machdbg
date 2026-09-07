#include <Windows.h>

extern "C" __declspec(dllexport) HMODULE LoadLibraryCheckedW(const wchar_t* szDll, bool allowFailure)
{
    return LoadLibraryW(szDll);
}

extern "C" __declspec(dllexport) HMODULE LoadLibraryCheckedA(const char* szDll, bool allowFailure)
{
    return LoadLibraryA(szDll);
}
