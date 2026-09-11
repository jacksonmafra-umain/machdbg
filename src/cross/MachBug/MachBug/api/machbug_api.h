#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MACHBUG_BUILDING
#define MACHBUG_EXPORT __attribute__((visibility("default")))
#else
#define MACHBUG_EXPORT
#endif

typedef enum
{
    DbgStatus_Ok = 0,
    DbgStatus_Failed = 1,
    DbgStatus_InvalidArgument = 2,
    DbgStatus_NotAttached = 3,
    DbgStatus_NotPermitted = 4,   /* task_for_pid denied; see the debuggability report */
    DbgStatus_NotSupported = 5,
    DbgStatus_TargetExited = 6,
} DbgStatus;

typedef enum
{
    DbgArch_Unknown = 0,
    DbgArch_X86_64 = 1,
    DbgArch_I386 = 2,
    DbgArch_Arm64 = 3,
    DbgArch_Arm64e = 4,
} DbgArch;

typedef struct
{
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rbp, rsp, rsi, rdi;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip;
    uint64_t rflags;
    uint16_t cs, ds, es, fs, gs, ss;
    uint64_t fs_base, gs_base;
} DbgRegsX86_64;

typedef struct
{
    uint64_t x[31];    /* x0 through x30; x30 is the link register */
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
} DbgRegsArm64;

typedef struct
{
    DbgArch arch;
    union
    {
        DbgRegsX86_64 x86_64;
        DbgRegsArm64 arm64;
    };
} DbgRegisters;

typedef enum
{
    DbgRegisterFlag_None = 0,
    DbgRegisterFlag_General = 1 << 0,
    DbgRegisterFlag_Flags = 1 << 1,
    DbgRegisterFlag_Vector = 1 << 2,
    DbgRegisterFlag_ProgramCounter = 1 << 3,
    DbgRegisterFlag_StackPointer = 1 << 4,
} DbgRegisterFlag;

/* Views iterate this table instead of reading struct fields, so no view needs to know
   which architecture it is displaying. */
typedef struct
{
    uint32_t id;
    const char* name;
    uint16_t bits;
    /* Byte offset into the ACTIVE ARM of DbgRegisters, not into DbgRegisters itself: a consumer
       reads a register as `*(uint64_t*)((char*)&regs.arm64 + offset)` for DbgArch_Arm64 and from
       &regs.x86_64 for DbgArch_X86_64, choosing the arm by regs.arch. Offsetting from &regs
       instead reads the wrong bytes by however much the arch tag and the union's padding
       occupy -- a mistake nothing at runtime can catch, which is why it is spelled out here. */
    uint16_t offset;
    uint32_t flags;     /* a mask of DbgRegisterFlag */
} DbgRegisterDesc;

typedef enum
{
    DbgBreakpointKind_Software = 0,
    DbgBreakpointKind_HwExec = 1,
    DbgBreakpointKind_HwRead = 2,
    DbgBreakpointKind_HwWrite = 3,
} DbgBreakpointKind;

typedef struct
{
    const char* path;
    const char* const* argv;    /* NULL terminated, may be NULL */
    const char* const* envp;    /* NULL terminated, may be NULL */
    const char* workingDirectory;
    pid_t attachPid;            /* 0 to launch rather than attach */
} DbgLaunchSpec;

typedef struct
{
    uint64_t base;
    uint64_t size;
    uint32_t protection;    /* current protection, VM_PROT_* bits */
    uint32_t maxProtection;
    uint32_t userTag;       /* VM_MEMORY_* */
    uint32_t depth;         /* 0 for the task's own map, deeper inside a submap */

    /* A readable name for userTag, in static storage owned by the engine -- never null, valid
       for the life of the process, and never freed by the caller. "untagged" rather than
       "unknown" for tag 0: measured, that is what an executable's own __TEXT carries. */
    const char* tagName;

    /* The load address of the module this region belongs to, or 0 for a region that belongs to
       none. NOT derived from userTag -- the main executable's __TEXT is untagged, so a map that
       trusted the tag would leave the one row a user looks for first unattributed.

       Zero for every region at the target's FIRST stop, and that is not a bug: dyld publishes
       its image list as it goes, and the first stop happens before any of it exists (see
       core/Modules.h). A caller that draws a memory map there gets tags and protections but no
       module names, and gets them at the next stop. */
    uint64_t moduleBase;
} DbgMemoryRegion;

typedef struct
{
    uint64_t base;          /* where the image is loaded */
    uint64_t size;          /* the sum of its segments' vm sizes */

    /* What the image moved by: base minus the __TEXT address it was linked at. A link-time
       address plus this is a runtime one, which is the only reason a module list is worth
       having. */
    uint64_t slide;

    uint8_t uuid[16];
    bool hasUuid;

    /* Inline, so nothing has to be freed. A path can name a file that does not exist: system
       dylibs live in the dyld shared cache, and /usr/lib/libSystem.B.dylib is loaded in every
       process while no such file is on disk. */
    char path[1024];
} DbgModule;

typedef struct
{
    uint64_t id;            /* the stable system thread id, what GetRegisters' threadId means */
    uint64_t pc;            /* the program counter, pointer-authentication stripped on arm64 */
    uint32_t runState;      /* TH_STATE_* as the kernel reports it */

    /* runState as words, in static storage owned by the engine -- never null, never freed. */
    const char* runStateName;

    /* Whether this is the thread the current stop is on. MEASURED: the kernel's run state does
       NOT answer that question. A thread parked in an unanswered exception reply reads as
       TH_STATE_WAITING with a suspend count of 0, and so does every thread of a task that
       Pause() has suspended -- the kernel describes what the thread was doing when it was
       frozen, not that it is frozen. Only the engine knows, so the engine says. */
    bool isStoppedThread;

    /* Empty unless the thread named itself. Inline, so nothing has to be freed. */
    char name[64];
} DbgThread;

typedef void (*DbgCbCreateProcess)(pid_t pid, uint64_t entryPoint, void* userdata);
typedef void (*DbgCbExitProcess)(int exitCode, void* userdata);
typedef void (*DbgCbSystemBreakpoint)(void* userdata);
typedef void (*DbgCbBreakpoint)(uint64_t address, void* userdata);
typedef void (*DbgCbStep)(void* userdata);
typedef void (*DbgCbPaused)(void* userdata);
/* The target is running again after a stop. A backend fires this only when resuming is
   asynchronous to the Continue/StepInto call that asked for it -- MachBug's stop is an
   unanswered exception reply, so the target runs when the engine's own loop thread sends
   that reply, not when Continue returns. A backend whose resume completes inside Continue
   (ptrace) leaves this unfired, and a caller that only needs "my Continue succeeded"
   should read Continue's status instead of waiting for this. */
typedef void (*DbgCbResumed)(void* userdata);
typedef void (*DbgCbError)(const char* error, void* userdata);
typedef void (*DbgCbDebugString)(const char* text, void* userdata);
typedef void (*DbgCbLoadModule)(uint64_t base, const char* path, void* userdata);
typedef void (*DbgCbUnloadModule)(uint64_t base, void* userdata);
/* threadId, everywhere it appears in this contract, is a stable per-thread identity that lives
   as long as the thread does -- on macOS, the system thread id. It is deliberately not a Mach
   port name: those are minted afresh by every task_threads call, so a port-shaped id names a
   thread only until the next time anyone looks. Passing 0 to GetRegisters/SetRegister means the
   thread the target is currently stopped on. */
typedef void (*DbgCbThreadCreate)(uint64_t threadId, void* userdata);
typedef void (*DbgCbThreadExit)(uint64_t threadId, void* userdata);
typedef void (*DbgCbException)(uint32_t type, uint64_t address, void* userdata);

typedef struct
{
    DbgCbCreateProcess onCreateProcess;
    DbgCbExitProcess onExitProcess;
    DbgCbSystemBreakpoint onSystemBreakpoint;
    DbgCbBreakpoint onBreakpoint;
    DbgCbStep onStep;
    DbgCbPaused onPaused;
    DbgCbResumed onResumed;
    DbgCbError onError;
    DbgCbDebugString onDebugString;
    DbgCbLoadModule onLoadModule;
    DbgCbUnloadModule onUnloadModule;
    DbgCbThreadCreate onThreadCreate;
    DbgCbThreadExit onThreadExit;
    DbgCbException onException;
    void* userdata;
} DbgEngineCallbacks;

/* The adapter holds this, never a backend type. Start blocks and owns the event loop;
   Continue, StepInto, Pause and Stop are callable from another thread. */
typedef struct DbgEngine
{
    void* impl;

    DbgStatus (*Start)(void* impl, const DbgLaunchSpec* spec);
    DbgStatus (*Continue)(void* impl);
    DbgStatus (*StepInto)(void* impl);
    DbgStatus (*Pause)(void* impl);
    DbgStatus (*Stop)(void* impl);

    DbgStatus (*GetRegisters)(void* impl, uint64_t threadId, DbgRegisters* out);
    DbgStatus (*SetRegister)(void* impl, uint64_t threadId, const char* name, uint64_t value);
    const DbgRegisterDesc* (*GetRegisterDescs)(void* impl, DbgArch arch, uint32_t* count);
    pid_t (*GetPid)(void* impl);
    DbgArch (*GetArch)(void* impl);

    DbgStatus (*MemRead)(void* impl, uint64_t addr, void* dest, uint64_t size);
    DbgStatus (*MemWrite)(void* impl, uint64_t addr, const void* src, uint64_t size);
    DbgStatus (*MemFindBaseAddr)(void* impl, uint64_t addr, uint64_t* base, uint64_t* size);
    bool (*MemIsCodePtr)(void* impl, uint64_t addr);
    bool (*MemIsValidPtr)(void* impl, uint64_t addr);
    DbgStatus (*MemEnumRegions)(void* impl, DbgMemoryRegion* out, uint32_t capacity,
            uint32_t* count);

    /* Fills up to `capacity` threads and writes how many exist through `count`. A caller that
       gets `capacity` back should ask again with more room -- the same shape MemEnumRegions
       uses, and for the same reason: the engine does not own a buffer on the caller's behalf. */
    DbgStatus (*ThreadEnum)(void* impl, DbgThread* out, uint32_t capacity, uint32_t* count);

    /* Fills up to `capacity` modules and writes how many exist through `count`. Empty at the
       target's first stop, which is not a failure: dyld publishes its image list as it goes and
       that stop happens before any of it exists. */
    DbgStatus (*ModEnum)(void* impl, DbgModule* out, uint32_t capacity, uint32_t* count);

    /* The load address of the module containing `addr`. DbgStatus_InvalidArgument, with a
       diagnostic, for an address in no module -- which is most of an address space: the stack,
       the heap and every anonymous mapping. Never the nearest module below it; answering "your
       stack is in libSystem" would be a lie of the most convincing kind. */
    DbgStatus (*ModBaseFromAddr)(void* impl, uint64_t addr, uint64_t* base);
    DbgStatus (*ModNameFromAddr)(void* impl, uint64_t addr, char* buf, uint64_t bufSize);

    DbgStatus (*SetBreakpoint)(void* impl, DbgBreakpointKind kind, uint64_t addr, uint32_t size);
    DbgStatus (*DeleteBreakpoint)(void* impl, uint64_t addr);

    /* Takes a breakpoint out of the target, or puts it back, without forgetting it. Distinct
       from DeleteBreakpoint on purpose: a user unticking a checkbox still wants the row, the
       address and the kind to survive, and delete-then-add-again is not the same operation --
       it loses the entry if the second call fails, and a hardware one may come back in a
       different slot. Disabled means the target is no longer carrying it, which is what
       IsBreakpointEffective then reports. */
    DbgStatus (*SetBreakpointEnabled)(void* impl, uint64_t addr, bool enabled);
    bool (*IsBreakpointEffective)(void* impl, uint64_t addr);
    uint32_t (*GetHwBreakpointSlots)(void* impl);
} DbgEngine;

MACHBUG_EXPORT DbgEngine* MachBugCreate(const DbgEngineCallbacks* callbacks);
MACHBUG_EXPORT void MachBugDestroy(DbgEngine* engine);

/* Detail for the last DbgStatus returned on this thread. Never a raw Mach kernel return code. */
MACHBUG_EXPORT const char* DbgLastErrorString(void);

#ifdef __cplusplus
}
#endif
