#include <MachBug/arch/Arm64.h>

#include <mach/mach_error.h>
#include <mach/exception_types.h>
#include <mach/thread_status.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

// arm_thread_state64_t exists only in a build *for* arm64: mach/arm/thread_status.h guards its
// contents behind __arm__ || __arm64__. The descriptor table below is built from DbgRegsArm64's
// own offsets and stays portable, because Descriptors(DbgArch_Arm64) has to answer on the Intel
// runner too -- only the state access is gated.
#if defined(__arm64__) || defined(__aarch64__)
#include <mach/arm/thread_status.h>
#endif

namespace MachBug::arch::Arm64
{
    namespace
    {
        constexpr uint32_t kGeneralCount = 31;   // x0 through x30
        constexpr uint32_t kDescriptorCount = kGeneralCount + 3;  // + sp, pc, pstate

        // Built once, at first use, rather than written out as 34 literal initialisers: the
        // offsets are exactly offsetof() arithmetic and the names are exactly "x0".."x30", so
        // spelling them by hand only creates a place for a typo to hide.
        struct Table
        {
            DbgRegisterDesc entries[kDescriptorCount];
            char names[kGeneralCount][4];

            Table()
            {
                uint32_t index = 0;
                for(uint32_t i = 0; i < kGeneralCount; ++i, ++index)
                {
                    std::snprintf(names[i], sizeof(names[i]), "x%u", i);
                    entries[index] = DbgRegisterDesc{
                        index, names[i], 64,
                        static_cast<uint16_t>(offsetof(DbgRegsArm64, x) + i * sizeof(uint64_t)),
                        DbgRegisterFlag_General};
                }
                entries[index] = DbgRegisterDesc{index, "sp", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, sp)),
                    DbgRegisterFlag_General | DbgRegisterFlag_StackPointer};
                ++index;
                entries[index] = DbgRegisterDesc{index, "pc", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, pc)),
                    DbgRegisterFlag_General | DbgRegisterFlag_ProgramCounter};
                ++index;
                entries[index] = DbgRegisterDesc{index, "pstate", 64,
                    static_cast<uint16_t>(offsetof(DbgRegsArm64, pstate)),
                    DbgRegisterFlag_Flags};
            }
        };

        const Table& table()
        {
            static const Table instance;
            return instance;
        }

#if defined(__arm64__) || defined(__aarch64__)
        bool getState(mach_port_t thread, arm_thread_state64_t* state, std::string* error)
        {
            if(thread == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no thread to read registers from: the target is not stopped, or the "
                             "thread id names no thread of it";
                return false;
            }
            mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
            const kern_return_t kr = thread_get_state(thread, ARM_THREAD_STATE64,
                reinterpret_cast<thread_state_t>(state), &count);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = std::string("thread_get_state(ARM_THREAD_STATE64) failed: ") +
                             mach_error_string(kr);
                return false;
            }
            return true;
        }
#endif // defined(__arm64__) || defined(__aarch64__)
    }


    bool IsSingleStepTrap(const exception_type_t exception, const int64_t* code,
                          const uint32_t codeCnt)
    {
        // Two shapes, both measured, and the second one is why this function exists at all.
        //
        // MEASURED, and not what the first version of this predicate assumed: a step armed at the
        // target's first stop completes as EXC_BREAKPOINT, but a step armed to get *off* a
        // software breakpoint completes as a signal passthrough -- EXC_SOFTWARE carrying
        // EXC_SOFT_SIGNAL and SIGTRAP, which is how PT_ATTACHEXC routes the debug trap the kernel
        // raised. Observed as `Exception(type=5,code1=5)` in a timeline where the step was
        // expected; see tests/breakpoints_software.cpp.
        //
        // Accepting that form is safe because every caller asks this only while a step is armed:
        // a target raising SIGTRAP on its own, with no step pending, never reaches here. Without
        // it, the completion falls through to cbException and the engine parks a target it was in
        // the middle of stepping.
        constexpr int64_t kExcSoftSignal = 0x10003;
        constexpr int64_t kSigTrap = 5;
        if(exception == EXC_SOFTWARE && codeCnt >= 2 && code != nullptr &&
           code[0] == kExcSoftSignal && code[1] == kSigTrap)
            return true;

        // The other shape: EXC_ARM_BREAKPOINT (1), which is what both a completed single-step and
        // a BRK instruction arrive as -- the subcode does not separate them, so this is necessary
        // here and the breakpoint table is what makes it sufficient in the exception loop.
        // Spelled out rather than included because mach/arm/exception.h:83 is gated on
        // __arm__ || __arm64__ and an x86-64 build still compiles this file.
        constexpr int64_t kExcArmBreakpoint = 1;
        return exception == EXC_BREAKPOINT && codeCnt >= 1 && code != nullptr &&
               code[0] == kExcArmBreakpoint;
    }

    const DbgRegisterDesc* Descriptors(uint32_t* count)
    {
        if(count)
            *count = kDescriptorCount;
        return table().entries;
    }

    uint64_t StripPointerAuth(const uint64_t address)
    {
        // MEASURED, not assumed. The SDK's arm_thread_state64_get_pc() authenticates a pointer
        // only when the *reading* binary is built for arm64e (mach/arm/_structs.h), and MachBug
        // is plain arm64, so nothing strips this for us.
        //
        // What was then measured, with run_endlessly stopped at its first exception: the pc that
        // arrives through thread_get_state is a plain address (0x00000001030a09c0 in that run),
        // with nothing set above bit 47, and lr is zero. So this mask is a no-op on the targets
        // this milestone debugs. It stays as the single seam anyway, because that is what spec
        // section 6 requires: the day a signed pc does arrive -- from an arm64e target built
        // with pointer authentication, which none of these fixtures is -- the strip belongs here
        // and nowhere else, not at each call site that displays or disassembles an address.
        return address & 0x0000007FFFFFFFFFull;
    }

#if defined(__arm64__) || defined(__aarch64__)

    bool Read(const mach_port_t thread, DbgRegisters* out, std::string* error)
    {
        if(!out)
        {
            if(error)
                *error = "no DbgRegisters to fill";
            return false;
        }
        arm_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        *out = DbgRegisters{};
        out->arch = DbgArch_Arm64;
        // __x holds 29 entries -- x0 through x28. x29 and x30 are the separate __fp and __lr
        // fields, which is the one place this mapping is not a copy.
        for(int i = 0; i < 29; ++i)
            out->arm64.x[i] = state.__x[i];
        out->arm64.x[29] = state.__fp;
        out->arm64.x[30] = state.__lr;
        out->arm64.sp = state.__sp;
        out->arm64.pc = StripPointerAuth(state.__pc);
        out->arm64.pstate = state.__cpsr;
        return true;
    }

    bool Write(const mach_port_t thread, const char* name, const uint64_t value,
               std::string* error)
    {
        if(!name)
        {
            if(error)
                *error = "no register name given";
            return false;
        }
        arm_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        const std::string wanted(name);
        bool assigned = false;
        for(uint32_t i = 0; i < 29 && !assigned; ++i)
        {
            if(wanted == table().names[i])
            {
                state.__x[i] = value;
                assigned = true;
            }
        }
        if(!assigned && wanted == "x29") { state.__fp = value; assigned = true; }
        if(!assigned && wanted == "x30") { state.__lr = value; assigned = true; }
        if(!assigned && wanted == "sp") { state.__sp = value; assigned = true; }
        if(!assigned && wanted == "pc") { state.__pc = value; assigned = true; }
        if(!assigned && wanted == "pstate")
        {
            state.__cpsr = static_cast<uint32_t>(value);
            assigned = true;
        }

        if(!assigned)
        {
            if(error)
                *error = "no arm64 register named '" + wanted + "'";
            return false;
        }

        const kern_return_t kr = thread_set_state(thread, ARM_THREAD_STATE64,
            reinterpret_cast<thread_state_t>(&state), ARM_THREAD_STATE64_COUNT);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(ARM_THREAD_STATE64) failed: ") +
                         mach_error_string(kr);
            return false;
        }
        return true;
    }

    bool SetSingleStep(const mach_port_t thread, const bool enable, std::string* error)
    {
        if(thread == MACH_PORT_NULL)
        {
            if(error)
                *error = "no thread to single-step: the target is not stopped, or the thread id "
                         "names no thread of it";
            return false;
        }

        // ARM_DEBUG_STATE64 is flavor 15, and the struct is bvr/bcr/wvr/wcr plus mdscr_el1. This
        // function touches only the last of those, and reads the state before writing it back for
        // that reason: hardware breakpoints and watchpoints live in the same state, so neither
        // this nor they may clobber the other's registers by writing a zeroed struct.
        arm_debug_state64_t state{};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        const kern_return_t got = thread_get_state(thread, ARM_DEBUG_STATE64,
            reinterpret_cast<thread_state_t>(&state), &count);
        if(got != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_get_state(ARM_DEBUG_STATE64) failed: ") +
                         mach_error_string(got);
            return false;
        }

        if(enable)
            state.__mdscr_el1 |= 1ULL;   // MDSCR_EL1 bit 0: SS
        else
            state.__mdscr_el1 &= ~1ULL;

        const kern_return_t set = thread_set_state(thread, ARM_DEBUG_STATE64,
            reinterpret_cast<thread_state_t>(&state), ARM_DEBUG_STATE64_COUNT);
        if(set != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(ARM_DEBUG_STATE64) failed: ") +
                         mach_error_string(set);
            return false;
        }
        return true;
    }

#else

    // Not "unimplemented": there is no arm64 thread state to read on x86-64 hardware.
    // thread_get_state has no flavor for it and the SDK has no struct for it. The descriptor
    // table above still answers, because a table is data.
    bool Read(mach_port_t, DbgRegisters*, std::string* error)
    {
        if(error)
            *error = "this build cannot read arm64 thread state: it is not an arm64 build";
        return false;
    }

    bool Write(mach_port_t, const char*, uint64_t, std::string* error)
    {
        if(error)
            *error = "this build cannot write arm64 thread state: it is not an arm64 build";
        return false;
    }

    bool SetSingleStep(mach_port_t, bool, std::string* error)
    {
        if(error)
            *error = "this build cannot single-step arm64 threads: it is not an arm64 build";
        return false;
    }

#endif // defined(__arm64__) || defined(__aarch64__)
}
