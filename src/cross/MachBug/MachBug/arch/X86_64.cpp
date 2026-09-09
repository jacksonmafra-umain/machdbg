#include <MachBug/arch/X86_64.h>

#include <mach/mach_error.h>
#include <mach/thread_status.h>

#include <cstddef>
#include <cstring>

// x86_thread_state64_t exists only in a build *for* x86-64: mach/i386/thread_status.h guards its
// contents behind __i386__ || __x86_64__. The descriptor table below is built from
// DbgRegsX86_64's own offsets and stays portable, because a view on Apple Silicon still has to
// be able to learn the x86-64 register file -- only the state access is gated.
#if defined(__x86_64__)
#include <mach/i386/thread_status.h>
#endif

namespace MachBug::arch::X86_64
{
    namespace
    {
        // One row per register the thread state can actually fill. x86_thread_state64_t carries
        // __cs, __fs and __gs and *no* __ds/__es/__ss, while DbgRegsX86_64 declares all six --
        // so ds, es and ss are deliberately absent rather than published as rows that always
        // read zero. A row a view cannot trust is worse than a row it never sees; a later
        // milestone that finds a source for them adds them here.
        struct Row
        {
            const char* name;
            uint16_t offset;    // into DbgRegsX86_64
            uint16_t bits;
            uint32_t flags;
        };

        #define MACHBUG_ROW(field, bits, flags) \
            Row{#field, static_cast<uint16_t>(offsetof(DbgRegsX86_64, field)), bits, flags}

        const Row kRows[] = {
            MACHBUG_ROW(rax, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rbx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rcx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rdx, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rbp, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rsp, 64, DbgRegisterFlag_General | DbgRegisterFlag_StackPointer),
            MACHBUG_ROW(rsi, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rdi, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r8,  64, DbgRegisterFlag_General),
            MACHBUG_ROW(r9,  64, DbgRegisterFlag_General),
            MACHBUG_ROW(r10, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r11, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r12, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r13, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r14, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(r15, 64, DbgRegisterFlag_General),
            MACHBUG_ROW(rip, 64, DbgRegisterFlag_General | DbgRegisterFlag_ProgramCounter),
            MACHBUG_ROW(rflags, 64, DbgRegisterFlag_Flags),
            MACHBUG_ROW(cs, 16, DbgRegisterFlag_General),
            MACHBUG_ROW(fs, 16, DbgRegisterFlag_General),
            MACHBUG_ROW(gs, 16, DbgRegisterFlag_General),
        };

        #undef MACHBUG_ROW

        constexpr uint32_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        // Descriptors are the rows without the state offsets, which are this file's business.
        struct Table
        {
            DbgRegisterDesc entries[kRowCount];

            Table()
            {
                for(uint32_t i = 0; i < kRowCount; ++i)
                    entries[i] = DbgRegisterDesc{i, kRows[i].name, kRows[i].bits,
                                                 kRows[i].offset, kRows[i].flags};
            }
        };

        const Table& table()
        {
            static const Table instance;
            return instance;
        }

#if defined(__x86_64__)
        // The state offsets, in the same order as kRows, so one index addresses both. Keeping
        // them in a parallel array rather than a field of Row is what keeps Row compilable on a
        // host whose SDK has no x86_thread_state64_t at all.
        const size_t kStateOffsets[] = {
            offsetof(x86_thread_state64_t, __rax), offsetof(x86_thread_state64_t, __rbx),
            offsetof(x86_thread_state64_t, __rcx), offsetof(x86_thread_state64_t, __rdx),
            offsetof(x86_thread_state64_t, __rbp), offsetof(x86_thread_state64_t, __rsp),
            offsetof(x86_thread_state64_t, __rsi), offsetof(x86_thread_state64_t, __rdi),
            offsetof(x86_thread_state64_t, __r8),  offsetof(x86_thread_state64_t, __r9),
            offsetof(x86_thread_state64_t, __r10), offsetof(x86_thread_state64_t, __r11),
            offsetof(x86_thread_state64_t, __r12), offsetof(x86_thread_state64_t, __r13),
            offsetof(x86_thread_state64_t, __r14), offsetof(x86_thread_state64_t, __r15),
            offsetof(x86_thread_state64_t, __rip), offsetof(x86_thread_state64_t, __rflags),
            offsetof(x86_thread_state64_t, __cs),  offsetof(x86_thread_state64_t, __fs),
            offsetof(x86_thread_state64_t, __gs),
        };

        // The two arrays are addressed by one index, so a row added to one and forgotten in the
        // other is a build failure rather than a register that reads someone else's value.
        static_assert(sizeof(kStateOffsets) / sizeof(kStateOffsets[0]) == kRowCount,
                      "every published x86-64 register needs a thread-state offset");

        uint64_t* fieldOf(x86_thread_state64_t* state, const uint32_t index)
        {
            return reinterpret_cast<uint64_t*>(reinterpret_cast<uint8_t*>(state) +
                                               kStateOffsets[index]);
        }

        bool getState(mach_port_t thread, x86_thread_state64_t* state, std::string* error)
        {
            if(thread == MACH_PORT_NULL)
            {
                if(error)
                    *error = "no thread to read registers from: the target is not stopped, or the "
                             "thread id names no thread of it";
                return false;
            }
            mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
            const kern_return_t kr = thread_get_state(thread, x86_THREAD_STATE64,
                reinterpret_cast<thread_state_t>(state), &count);
            if(kr != KERN_SUCCESS)
            {
                if(error)
                    *error = std::string("thread_get_state(x86_THREAD_STATE64) failed: ") +
                             mach_error_string(kr);
                return false;
            }
            return true;
        }
#endif // defined(__x86_64__)
    }

    const DbgRegisterDesc* Descriptors(uint32_t* count)
    {
        if(count)
            *count = kRowCount;
        return table().entries;
    }

#if defined(__x86_64__)

    bool Read(const mach_port_t thread, DbgRegisters* out, std::string* error)
    {
        if(!out)
        {
            if(error)
                *error = "no DbgRegisters to fill";
            return false;
        }
        x86_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        *out = DbgRegisters{};
        out->arch = DbgArch_X86_64;
        for(uint32_t i = 0; i < kRowCount; ++i)
        {
            // Every field of x86_thread_state64_t is 64 bits wide even where the register is not
            // (__cs, __fs, __gs), so one read shape covers the whole table; the descriptor's
            // `bits` is what tells a view how much of it to show.
            const uint64_t value = *fieldOf(&state, i);
            std::memcpy(reinterpret_cast<uint8_t*>(&out->x86_64) + kRows[i].offset, &value,
                        sizeof(value));
        }
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
        x86_thread_state64_t state{};
        if(!getState(thread, &state, error))
            return false;

        const std::string wanted(name);
        uint32_t found = kRowCount;
        for(uint32_t i = 0; i < kRowCount; ++i)
        {
            if(wanted == kRows[i].name)
                found = i;
        }
        if(found == kRowCount)
        {
            if(error)
                *error = "no x86-64 register named '" + wanted + "'";
            return false;
        }

        *fieldOf(&state, found) = value;
        const kern_return_t kr = thread_set_state(thread, x86_THREAD_STATE64,
            reinterpret_cast<thread_state_t>(&state), x86_THREAD_STATE64_COUNT);
        if(kr != KERN_SUCCESS)
        {
            if(error)
                *error = std::string("thread_set_state(x86_THREAD_STATE64) failed: ") +
                         mach_error_string(kr);
            return false;
        }
        return true;
    }

#else

    // Not "unimplemented": there is no such thing as reading an x86-64 thread state on arm64
    // hardware. thread_get_state has no flavor for it, the SDK has no struct for it, and a
    // target running under Rosetta exposes the translator's arm64 state rather than the emulated
    // one's. The descriptor table above still answers, because a table is data.
    bool Read(mach_port_t, DbgRegisters*, std::string* error)
    {
        if(error)
            *error = "this build cannot read x86-64 thread state: it is not an x86-64 build";
        return false;
    }

    bool Write(mach_port_t, const char*, uint64_t, std::string* error)
    {
        if(error)
            *error = "this build cannot write x86-64 thread state: it is not an x86-64 build";
        return false;
    }

#endif // defined(__x86_64__)
}
