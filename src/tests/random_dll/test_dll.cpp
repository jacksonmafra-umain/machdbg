#include <windows.h>
#include <stdio.h>

extern "C" void __declspec(dllexport) TestFunction(const char* message)
{
    printf("TestFunction: %s\n", message);
}

extern "C" BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch(fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        printf("Random DLL loaded! Instance: 0x%p\n", hinstDLL);
        break;
    case DLL_PROCESS_DETACH:
        printf("Random DLL unloaded!\n");
        break;
    default:
        break;
    }

    return TRUE;
}
