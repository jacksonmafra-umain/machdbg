#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using TestFunction = void (*)(const char* message);

static void generateRandomString(wchar_t* buffer, int length)
{
    const wchar_t charset[] = L"abcdefghijklmnopqrstuvwxyz0123456789";
    const int charsetSize = (int)(_countof(charset) - 1);
    for(int i = 0; i < length; i++)
        buffer[i] = charset[rand() % charsetSize];
    buffer[length] = L'\0';
}

static bool copyDllWithRandomName(const wchar_t* sourceDll, wchar_t* finalPath, size_t finalPathCount)
{
    wchar_t tempPath[MAX_PATH] = L"";
    wchar_t randomName[16] = L"";
    if(GetTempPathW(_countof(tempPath), tempPath) == 0)
        return false;

    generateRandomString(randomName, 8);
    if(swprintf_s(finalPath, finalPathCount, L"%s%s.xxl", tempPath, randomName) < 0)
        return false;

    return CopyFileW(sourceDll, finalPath, FALSE) != FALSE;
}

int wmain()
{
    srand(1337);

    wchar_t sourceDll[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, sourceDll, _countof(sourceDll));
    if(auto slashIdx = wcsrchr(sourceDll, L'\\'))
        slashIdx[1] = L'\0';
    wcscat_s(sourceDll, L"random.dll");

    if(GetFileAttributesW(sourceDll) == INVALID_FILE_ATTRIBUTES)
        return 1;

    wchar_t dllPath1[MAX_PATH] = L"";
    wchar_t dllPath2[MAX_PATH] = L"";

    if(!copyDllWithRandomName(sourceDll, dllPath1, _countof(dllPath1)))
        return 1;
    auto hDll1 = LoadLibraryExW(dllPath1, nullptr, 0);
    if(hDll1 == nullptr)
    {
        DeleteFileW(dllPath1);
        return 1;
    }
    if(auto testFunc = (TestFunction)GetProcAddress(hDll1, "TestFunction"))
        testFunc("Hello from first random DLL!");
    else
        return 1;
    FreeLibrary(hDll1);
    DeleteFileW(dllPath1);

    if(!copyDllWithRandomName(sourceDll, dllPath2, _countof(dllPath2)))
        return 1;
    auto hDll2 = LoadLibraryExW(dllPath2, nullptr, 0);
    if(hDll2 == nullptr)
    {
        DeleteFileW(dllPath2);
        return 1;
    }
    if(auto testFunc = (TestFunction)GetProcAddress(hDll2, "TestFunction"))
        testFunc("Hello from second random DLL!");
    else
        return 1;
    FreeLibrary(hDll2);
    DeleteFileW(dllPath2);

    __debugbreak();
    return 0;
}
