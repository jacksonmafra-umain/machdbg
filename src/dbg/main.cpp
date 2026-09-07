/**
 @file main.cpp

 @brief Implements the main class.
 */

#include "debugger.h"
#include "threading.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch(fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        hInst = hinstDLL;

        // Get program directory
        strcpy_s(szUserDir, StringUtils::Utf16ToUtf8(BridgeUserDirectory()).c_str());

        {
            wchar_t wszDir[deflen] = L"";
            if(GetModuleFileNameW(hInst, wszDir, deflen))
            {
                strcpy_s(szProgramDir, StringUtils::Utf16ToUtf8(wszDir).c_str());

                int len = (int)strlen(szProgramDir);
                while(szProgramDir[len] != '\\')
                    len--;
                szProgramDir[len] = 0;
            }
        }
    }

    default:
        break;
    }
    return TRUE;
}
