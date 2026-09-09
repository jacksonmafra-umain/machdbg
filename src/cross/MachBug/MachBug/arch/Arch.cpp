#include <MachBug/arch/Arch.h>

#include <MachBug/arch/Arm64.h>
#include <MachBug/arch/X86_64.h>

namespace MachBug::arch
{
    const DbgRegisterDesc* Descriptors(const DbgArch arch, uint32_t* count)
    {
        switch(arch)
        {
        case DbgArch_Arm64:
            return Arm64::Descriptors(count);
        case DbgArch_X86_64:
            return X86_64::Descriptors(count);
        default:
            // Nothing, and a zero count. Falling back to the host's table would make a wrong
            // register file look like a working one, which is the one outcome a table exists to
            // prevent -- DbgArch_Arm64e and DbgArch_I386 are declared in machbug_api.h but have
            // no register file here yet.
            if(count)
                *count = 0;
            return nullptr;
        }
    }

    bool Read(const DbgArch arch, const mach_port_t thread, DbgRegisters* out, std::string* error)
    {
        switch(arch)
        {
        case DbgArch_Arm64:
            return Arm64::Read(thread, out, error);
        case DbgArch_X86_64:
            return X86_64::Read(thread, out, error);
        default:
            if(error)
                *error = "no register file for this architecture in this build";
            return false;
        }
    }

    bool Write(const DbgArch arch, const mach_port_t thread, const char* name,
               const uint64_t value, std::string* error)
    {
        switch(arch)
        {
        case DbgArch_Arm64:
            return Arm64::Write(thread, name, value, error);
        case DbgArch_X86_64:
            return X86_64::Write(thread, name, value, error);
        default:
            if(error)
                *error = "no register file for this architecture in this build";
            return false;
        }
    }

    bool IsSingleStepTrap(const DbgArch arch, const exception_type_t exception,
                          const int64_t* code, const uint32_t codeCnt)
    {
        switch(arch)
        {
        case DbgArch_Arm64:
            return Arm64::IsSingleStepTrap(exception, code, codeCnt);
        case DbgArch_X86_64:
            return X86_64::IsSingleStepTrap(exception, code, codeCnt);
        default:
            return false;
        }
    }

    bool SetSingleStep(const DbgArch arch, const mach_port_t thread, const bool enable,
                       std::string* error)
    {
        switch(arch)
        {
        case DbgArch_Arm64:
            return Arm64::SetSingleStep(thread, enable, error);
        case DbgArch_X86_64:
            return X86_64::SetSingleStep(thread, enable, error);
        default:
            if(error)
                *error = "no single-step mechanism for this architecture in this build";
            return false;
        }
    }
}
