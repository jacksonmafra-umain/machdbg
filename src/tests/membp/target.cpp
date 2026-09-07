#include <Windows.h>

#pragma section(".mbd2", read, write)
#pragma section(".mbd3", read, write)

extern "C"
{
    __declspec(allocate(".mbd2")) volatile unsigned char Padding[0x2000] = { 2 };
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile signed char ReadTarget = -1;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile signed char WriteTarget = 0;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile unsigned char* HeapPointer = nullptr;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile unsigned char* CrossPagePointer = nullptr;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile unsigned char* AdjacentPointer = nullptr;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile unsigned char* LargePointer = nullptr;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile LONG ExecCounter = 0;
    __declspec(allocate(".mbd3")) __declspec(dllexport) volatile DWORD StartMode = 0;

#pragma code_seg(push, membp_code, ".mbc")
    static DWORD WINAPI ThreadReadProc(void*)
    {
        return static_cast<DWORD>(static_cast<unsigned char>(ReadTarget));
    }

    __declspec(dllexport) __declspec(noinline) void ReadSequence()
    {
        const auto value = ReadTarget;
        ExitProcess(static_cast<UINT>(static_cast<unsigned char>(value)));
    }

    __declspec(dllexport) __declspec(noinline) void ReadTwiceSequence()
    {
        const auto first = static_cast<unsigned char>(ReadTarget);
        const auto second = static_cast<unsigned char>(ReadTarget);
        ExitProcess(static_cast<UINT>(first + second));
    }

    __declspec(dllexport) __declspec(noinline) void MultiThreadReadSequence()
    {
        HANDLE threads[2] = { nullptr, nullptr };
        threads[0] = CreateThread(nullptr, 0, ThreadReadProc, nullptr, 0, nullptr);
        threads[1] = CreateThread(nullptr, 0, ThreadReadProc, nullptr, 0, nullptr);
        if(threads[0] == nullptr || threads[1] == nullptr)
            ExitProcess(0xE3);

        WaitForMultipleObjects(2, threads, TRUE, INFINITE);

        DWORD thread0Code = 0;
        DWORD thread1Code = 0;
        GetExitCodeThread(threads[0], &thread0Code);
        GetExitCodeThread(threads[1], &thread1Code);
        CloseHandle(threads[0]);
        CloseHandle(threads[1]);
        ExitProcess(thread0Code + thread1Code);
    }

    __declspec(dllexport) __declspec(noinline) void WriteSequence()
    {
        WriteTarget = 0x5A;
        ExitProcess(0x5A);
    }

    __declspec(dllexport) __declspec(noinline) void ReadThenWriteSequence()
    {
        const auto value = static_cast<unsigned char>(ReadTarget);
        WriteTarget = 0x5A;
        ExitProcess(static_cast<UINT>(value + static_cast<unsigned char>(WriteTarget)));
    }

    __declspec(dllexport) __declspec(noinline) void ReadTwoTargetsSequence()
    {
        const auto first = static_cast<unsigned char>(ReadTarget);
        const auto second = static_cast<unsigned char>(WriteTarget);
        ExitProcess(static_cast<UINT>(first + second));
    }

    __declspec(dllexport) __declspec(noinline) void WriteReadTargetSequence()
    {
        ReadTarget = 0x5A;
        ExitProcess(0x5A);
    }

    __declspec(dllexport) __declspec(noinline) void WriteTwoTargetsSequence()
    {
        ReadTarget = 0x5A;
        WriteTarget = 0x4B;
        ExitProcess(0xA5);
    }

    __declspec(dllexport) __declspec(noinline) void StressAccessSequence()
    {
        unsigned int sum = 0;
        unsigned int state = 0x1234u;
        for(unsigned int i = 0; i < 16; i++)
        {
            state = state * 1103515245u + 12345u;
            if(state & 1)
                sum += static_cast<unsigned char>(WriteTarget);
            else
                WriteTarget = static_cast<signed char>(state & 0x7F);
        }
        ExitProcess(sum & 0xFF);
    }

    __declspec(dllexport) __declspec(noinline) void ExecSequence()
    {
        ExitProcess(0x33);
    }

    __declspec(dllexport) __declspec(noinline) void ExecLeaf()
    {
        InterlockedIncrement(const_cast<LONG*>(&ExecCounter));
    }

    __declspec(dllexport) __declspec(noinline) void ExecTwiceSequence()
    {
        ExecLeaf();
        ExecLeaf();
        ExitProcess(0x66);
    }

    __declspec(dllexport) __declspec(noinline) void WriteHeap()
    {
        *HeapPointer = 0xBC;
        ExitProcess(static_cast<UINT>(*HeapPointer));
    }

    __declspec(dllexport) __declspec(noinline) void StartHeap()
    {
        HANDLE hHeap = GetProcessHeap();
        HeapPointer = (unsigned char*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, 1024);
        WriteHeap();
    }

    __declspec(dllexport) __declspec(noinline) void CrossPageReadSequence()
    {
        const auto first = CrossPagePointer[0xFFF];
        const auto second = CrossPagePointer[0x1000];
        ExitProcess(static_cast<UINT>(first + second));
    }

    __declspec(dllexport) __declspec(noinline) void StartCrossPageRead()
    {
        CrossPagePointer = (unsigned char*)VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(CrossPagePointer == nullptr)
            ExitProcess(0xE1);
        CrossPagePointer[0xFFF] = 0x21;
        CrossPagePointer[0x1000] = 0x43;
        CrossPageReadSequence();
    }

    __declspec(dllexport) __declspec(noinline) void ThreePageReadSequence()
    {
        const auto first = CrossPagePointer[0xFFF];
        const auto second = CrossPagePointer[0x1000];
        const auto third = CrossPagePointer[0x2000];
        ExitProcess(static_cast<UINT>(first + second + third));
    }

    __declspec(dllexport) __declspec(noinline) void StartThreePageRead()
    {
        CrossPagePointer = (unsigned char*)VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(CrossPagePointer == nullptr)
            ExitProcess(0xE4);
        CrossPagePointer[0xFFF] = 0x11;
        CrossPagePointer[0x1000] = 0x22;
        CrossPagePointer[0x2000] = 0x33;
        ThreePageReadSequence();
    }

    __declspec(dllexport) __declspec(noinline) void CrossPageWriteSequence()
    {
        CrossPagePointer[0xFFF] = 0xAA;
        CrossPagePointer[0x1000] = 0x55;
        ExitProcess(0xFF);
    }

    __declspec(dllexport) __declspec(noinline) void StartCrossPageWrite()
    {
        CrossPagePointer = (unsigned char*)VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(CrossPagePointer == nullptr)
            ExitProcess(0xE2);
        CrossPagePointer[0xFFF] = 0;
        CrossPagePointer[0x1000] = 0;
        CrossPageWriteSequence();
    }

    __declspec(dllexport) __declspec(noinline) void AdjacentReadSequence()
    {
        const auto first = AdjacentPointer[0xFFFF];
        const auto second = AdjacentPointer[0x10000];
        ExitProcess(static_cast<UINT>(first + second));
    }

    __declspec(dllexport) __declspec(noinline) void StartAdjacentRead()
    {
        // Reserve and release one contiguous area, then reserve each 64 KiB half
        // separately. The resulting pages are adjacent but have different
        // AllocationBase values, so a single VirtualProtectEx cannot span them.
        auto reservation = (unsigned char*)VirtualAlloc(nullptr, 0x20000, MEM_RESERVE, PAGE_NOACCESS);
        if(reservation == nullptr || !VirtualFree(reservation, 0, MEM_RELEASE))
            ExitProcess(0xE5);

        auto first = (unsigned char*)VirtualAlloc(reservation, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        auto second = (unsigned char*)VirtualAlloc(reservation + 0x10000, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(first != reservation || second != reservation + 0x10000)
            ExitProcess(0xE6);

        AdjacentPointer = first;
        AdjacentPointer[0xFFFF] = 0x21;
        AdjacentPointer[0x10000] = 0x43;
        AdjacentReadSequence();
    }

    __declspec(dllexport) __declspec(noinline) void LargeRangeSequence()
    {
        const auto first = LargePointer[0];
        const auto last = LargePointer[0x8000000 - 1];
        ExitProcess(static_cast<UINT>(first + last));
    }

    __declspec(dllexport) __declspec(noinline) void StartLargeRange()
    {
        // 32K pages reproduces the large-range setup that originally exposed the
        // publish-before-metadata race and makes per-page syscall regressions visible.
        LargePointer = (unsigned char*)VirtualAlloc(nullptr, 0x8000000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(LargePointer == nullptr)
            ExitProcess(0xE7);
        LargePointer[0] = 0x31;
        LargePointer[0x8000000 - 1] = 0x37;
        LargeRangeSequence();
    }

    __declspec(noinline) void start()
    {
        switch(StartMode)
        {
        case 1:
            ReadSequence();
            break;
        case 2:
            ReadTwiceSequence();
            break;
        case 3:
            WriteSequence();
            break;
        case 4:
            ReadThenWriteSequence();
            break;
        case 5:
            ExecSequence();
            break;
        case 6:
            ExecTwiceSequence();
            break;
        case 7:
            StartHeap();
            break;
        case 8:
            StartCrossPageRead();
            break;
        case 9:
            StartCrossPageWrite();
            break;
        case 10:
        case 0x10:
            MultiThreadReadSequence();
            break;
        case 11:
        case 0x11:
            StartThreePageRead();
            break;
        case 12:
        case 0x12:
            StressAccessSequence();
            break;
        case 0x13:
            ReadTwoTargetsSequence();
            break;
        case 0x14:
            WriteReadTargetSequence();
            break;
        case 0x16:
            WriteTwoTargetsSequence();
            break;
        case 0x15:
            StartAdjacentRead();
            break;
        case 0x30:
            StartLargeRange();
            break;
        default:
            break;
        }
        ExitProcess(0);
    }
#pragma code_seg(pop, membp_code)
}
