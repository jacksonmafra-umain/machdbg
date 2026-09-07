#pragma once

#include <Windows.h>

bool InitializeSignatureCheck();

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

__declspec(dllexport) HMODULE LoadLibraryCheckedW(const wchar_t* szDll, bool allowFailure);
__declspec(dllexport) HMODULE LoadLibraryCheckedA(const char* szDll, bool allowFailure);

#ifdef __cplusplus
}
#endif // __cplusplus