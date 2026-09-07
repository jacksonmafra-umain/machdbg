#include <ElfBug/process/Process.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>

namespace ElfBug
{
    bool Process::MemRead(const ptr address, void* buffer, const ptr size, ptr* bytesRead) const
    {
        if(!buffer || !size)
            return false;

        iovec local{};
        local.iov_base = buffer;
        local.iov_len = size;

        iovec remote{};
        remote.iov_base = reinterpret_cast<void*>(address);
        remote.iov_len = size;

        ssize_t result = process_vm_readv(pid, &local, 1, &remote, 1, 0);
        if(result == -1)
        {
            const int fd = memFd();
            if(fd == -1)
            {
                memset(buffer, 0, size);
                return false;
            }

            result = pread(fd, buffer, size, static_cast<off_t>(address));
            if(result == -1)
            {
                memset(buffer, 0, size);
                return false;
            }
        }

        if(bytesRead)
            *bytesRead = static_cast<ptr>(result);
        if(static_cast<size_t>(result) < size)
            memset(static_cast<char*>(buffer) + result, 0, size - result);
        return static_cast<size_t>(result) == size;
    }

    bool Process::MemWrite(const ptr address, const void* buffer, const ptr size, ptr* bytesWritten) const
    {
        if(!buffer || !size)
            return false;

        iovec local{};
        local.iov_base = const_cast<void*>(buffer);
        local.iov_len = size;

        iovec remote{};
        remote.iov_base = reinterpret_cast<void*>(address);
        remote.iov_len = size;

        ssize_t result = process_vm_writev(pid, &local, 1, &remote, 1, 0);
        if(result == -1)
        {
            const int fd = memFd();
            if(fd == -1)
                return false;

            result = pwrite(fd, buffer, size, static_cast<off_t>(address));
            if(result == -1)
                return false;
        }

        if(bytesWritten)
            *bytesWritten = static_cast<ptr>(result);
        return static_cast<size_t>(result) == size;
    }

    bool Process::MemIsValidPtr(const ptr address) const
    {
        uint8 byte;
        return MemRead(address, &byte, 1);
    }

    bool Process::MemProtect(ptr address, ptr size, uint32 newProtect, const uint32* oldProtect)
    {
        // TODO: implement via ptrace or /proc/pid/mem mprotect
        (void)address;
        (void)size;
        (void)newProtect;
        (void)oldProtect;
        return false;
    }
}
