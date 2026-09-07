#include <windows.h>

#include <cstdio>
#include <cstring>

static HANDLE gParkSignal;

static DWORD WINAPI ParkedThread(LPVOID)
{
    // Block forever on a private event.
    HANDLE hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    WaitForSingleObject(hEvent, INFINITE);
    return 0;
}

static DWORD WINAPI SignaledParkedThread(LPVOID)
{
    // After the driver signals (post-attach), produce a debug event so this
    // thread becomes the debugger's active thread, then block forever.
    WaitForSingleObject(gParkSignal, INFINITE);
    OutputDebugStringA("attach_pause: parking");
    return ParkedThread(nullptr);
}

int main(int argc, char** argv)
{
    // In block mode every thread (including the main thread) blocks forever,
    // so a pause is only possible with a break-in thread.
    bool blockMode = argc > 1 && std::strcmp(argv[1], "block") == 0;

    // Create the message queue before signaling readiness.
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);

    gParkSignal = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CloseHandle(CreateThread(nullptr, 0, SignaledParkedThread, nullptr, 0, nullptr));
    for(int i = 0; i < 4; i++)
        CloseHandle(CreateThread(nullptr, 0, ParkedThread, nullptr, 0, nullptr));

    std::printf("ready %lu %lu\n", GetCurrentProcessId(), GetCurrentThreadId());
    std::fflush(stdout);

    if(blockMode)
        return (int)ParkedThread(nullptr); // the driver terminates the process

    // Pump messages like the main thread of a GUI application. The pause
    // command wakes this loop up with WM_NULL; the driver stops it with WM_QUIT
    // and triggers the signaled thread with WM_APP.
    while(GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if(msg.message == WM_APP)
            SetEvent(gParkSignal);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
