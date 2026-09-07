#include <windows.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>

extern "C" __declspec(dllexport) void* gBreakpointAddress = nullptr;
extern "C" __declspec(dllexport) volatile LONG gEscapedBreakpoints = 0;

static volatile LONG gReady = 0;
static volatile LONG gGo = 0;
static volatile LONG gPhase = 0;

#ifdef _WIN64
using TestInstruction = LONG(NTAPI*)(BOOLEAN alertable, PLARGE_INTEGER delay);
static TestInstruction gTestInstruction = nullptr;
#else
using TestInstruction = void(*)();
static TestInstruction gTestInstruction = nullptr;
#endif

static LONG CALLBACK BreakpointHandler(EXCEPTION_POINTERS* pointers)
{
    if(pointers->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT ||
            pointers->ExceptionRecord->ExceptionAddress != gBreakpointAddress)
        return EXCEPTION_CONTINUE_SEARCH;

    InterlockedIncrement(&gEscapedBreakpoints);
#ifdef _WIN64
    pointers->ContextRecord->Rip = reinterpret_cast<DWORD64>(gBreakpointAddress);
#else
    pointers->ContextRecord->Eip = reinterpret_cast<DWORD>(gBreakpointAddress);
#endif
    return EXCEPTION_CONTINUE_EXECUTION;
}

#ifdef _WIN64
static bool InitializeTestInstruction()
{
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    auto stub = reinterpret_cast<const uint8_t*>(GetProcAddress(ntdll, "NtDelayExecution"));
    if(stub == nullptr)
        return false;

    DWORD syscallNumber = 0;
    bool found = false;
    for(size_t i = 0; i + 5 < 32 && !found; ++i)
    {
        if(stub[i] != 0xB8)
            continue;
        for(size_t j = i + 5; j + 1 < 32; ++j)
        {
            if(stub[j] == 0x0F && stub[j + 1] == 0x05)
            {
                memcpy(&syscallNumber, stub + i + 1, sizeof(syscallNumber));
                found = true;
                break;
            }
        }
    }
    if(!found)
        return false;

    // mov r10,rcx; mov eax,syscallNumber; syscall; jmp done; nop; nop; nop; ret
    uint8_t code[] =
    {
        0x4C, 0x8B, 0xD1,
        0xB8, 0, 0, 0, 0,
        0x0F, 0x05,
        0xEB, 0x03,
        0x90, 0x90, 0x90,
        0xC3
    };
    memcpy(code + 4, &syscallNumber, sizeof(syscallNumber));

    auto memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(memory == nullptr)
        return false;
    memcpy(memory, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code));

    gTestInstruction = reinterpret_cast<TestInstruction>(memory);
    gBreakpointAddress = memory + 8; // syscall
    return true;
}
#else
static bool InitializeTestInstruction()
{
    const uint8_t code[] = { 0x90, 0xC3 }; // nop; ret
    auto memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if(memory == nullptr)
        return false;
    memcpy(memory, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), memory, sizeof(code));

    gTestInstruction = reinterpret_cast<TestInstruction>(memory);
    gBreakpointAddress = memory;
    return true;
}
#endif

static DWORD WINAPI Worker(void*)
{
    InterlockedIncrement(&gReady);
    while(InterlockedCompareExchange(&gGo, 0, 0) == 0)
        YieldProcessor();

#ifdef _WIN64
    LARGE_INTEGER delay;
    delay.QuadPart = -250LL * 10000LL;
    gTestInstruction(FALSE, &delay);
#else
    gTestInstruction();
#endif
    return 0;
}

extern "C" __declspec(dllexport) __declspec(noinline) void Ready()
{
    InterlockedExchange(&gPhase, 1);
}

extern "C" __declspec(dllexport) __declspec(noinline) void Finish()
{
    InterlockedExchange(&gPhase, 2);
}

int main()
{
    if(!InitializeTestInstruction())
        return 100;
    Ready();
    AddVectoredExceptionHandler(1, BreakpointHandler);

    constexpr DWORD ThreadCount = 64;
    HANDLE threads[ThreadCount] = {};
    for(DWORD i = 0; i < ThreadCount; ++i)
    {
        threads[i] = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if(threads[i] == nullptr)
            return 101;
    }

    while(InterlockedCompareExchange(&gReady, 0, 0) != ThreadCount)
        Sleep(0);
    InterlockedExchange(&gGo, 1);

    WaitForMultipleObjects(ThreadCount, threads, TRUE, INFINITE);
    for(auto thread : threads)
        CloseHandle(thread);

    Finish();
    return gEscapedBreakpoints == 0 ? 0 : 102;
}
