#pragma once

#include <cstdint>

struct alignas(16) XMMREGISTER
{
    uint64_t Low = 0;
    int64_t High = 0;
};

struct YMMREGISTER
{
    XMMREGISTER Low;
    XMMREGISTER High;
};

struct ZMMREGISTER
{
    YMMREGISTER Low;
    YMMREGISTER High;
};

struct X87FPU
{
    uint16_t ControlWord = 0;
    uint16_t StatusWord = 0;
    uint16_t TagWord = 0;
    uint32_t ErrorOffset = 0;
    uint32_t ErrorSelector = 0;
    uint32_t DataOffset = 0;
    uint32_t DataSelector = 0;
    uint32_t Cr0NpxState = 0;
};

struct X87FPUREGISTER
{
    uint8_t data[10] = {};
    int st_value = 0;
    int tag = 0;
};

struct FLAGS
{
    bool c = false;
    bool p = false;
    bool a = false;
    bool z = false;
    bool s = false;
    bool t = false;
    bool i = false;
    bool d = false;
    bool o = false;
};

struct MXCSRFIELDS
{
    bool FZ = false;
    bool PM = false;
    bool UM = false;
    bool OM = false;
    bool ZM = false;
    bool IM = false;
    bool DM = false;
    bool DAZ = false;
    bool PE = false;
    bool UE = false;
    bool OE = false;
    bool ZE = false;
    bool DE = false;
    bool IE = false;
    uint16_t RC = 0;
};

struct X87STATUSWORDFIELDS
{
    bool B = false;
    bool C3 = false;
    bool C2 = false;
    bool C1 = false;
    bool C0 = false;
    bool ES = false;
    bool SF = false;
    bool P = false;
    bool U = false;
    bool O = false;
    bool Z = false;
    bool D = false;
    bool I = false;
    uint16_t TOP = 0;
};

struct X87CONTROLWORDFIELDS
{
    bool IC = false;
    bool IEM = false;
    bool PM = false;
    bool UM = false;
    bool OM = false;
    bool ZM = false;
    bool DM = false;
    bool IM = false;
    uint16_t RC = 0;
    uint16_t PC = 0;
};

struct LASTERROR
{
    uint32_t code = 0;
    char name[128] = {};
};

struct LASTSTATUS
{
    uint32_t code = 0;
    char name[128] = {};
};

struct REGISTERCONTEXT
{
    uint64_t cax = 0;
    uint64_t ccx = 0;
    uint64_t cdx = 0;
    uint64_t cbx = 0;
    uint64_t csp = 0;
    uint64_t cbp = 0;
    uint64_t csi = 0;
    uint64_t cdi = 0;
    uint64_t r8 = 0;
    uint64_t r9 = 0;
    uint64_t r10 = 0;
    uint64_t r11 = 0;
    uint64_t r12 = 0;
    uint64_t r13 = 0;
    uint64_t r14 = 0;
    uint64_t r15 = 0;
    uint64_t cip = 0;
    uint64_t eflags = 0;
    uint16_t gs = 0;
    uint16_t fs = 0;
    uint16_t es = 0;
    uint16_t ds = 0;
    uint16_t cs = 0;
    uint16_t ss = 0;
    uint64_t dr0 = 0;
    uint64_t dr1 = 0;
    uint64_t dr2 = 0;
    uint64_t dr3 = 0;
    uint64_t dr6 = 0;
    uint64_t dr7 = 0;
    uint8_t RegisterArea[80] = {};
    X87FPU x87fpu;
    uint32_t MxCsr = 0;
    XMMREGISTER XmmRegisters[16] = {};
    YMMREGISTER YmmRegisters[16] = {};
};

struct REGISTERCONTEXT_AVX512
{
    uint64_t cax = 0;
    uint64_t ccx = 0;
    uint64_t cdx = 0;
    uint64_t cbx = 0;
    uint64_t csp = 0;
    uint64_t cbp = 0;
    uint64_t csi = 0;
    uint64_t cdi = 0;
    uint64_t r8 = 0;
    uint64_t r9 = 0;
    uint64_t r10 = 0;
    uint64_t r11 = 0;
    uint64_t r12 = 0;
    uint64_t r13 = 0;
    uint64_t r14 = 0;
    uint64_t r15 = 0;
    uint64_t cip = 0;
    uint64_t eflags = 0;
    uint64_t dr0 = 0;
    uint64_t dr1 = 0;
    uint64_t dr2 = 0;
    uint64_t dr3 = 0;
    uint64_t dr6 = 0;
    uint64_t dr7 = 0;
    ZMMREGISTER ZmmRegisters[32] = {};
    uint64_t Opmask[8] = {};
    uint8_t RegisterArea[80] = {};
    uint32_t MxCsr = 0;
    X87FPU x87fpu;
    uint16_t gs = 0;
    uint16_t fs = 0;
    uint16_t es = 0;
    uint16_t ds = 0;
    uint16_t cs = 0;
    uint16_t ss = 0;
};

struct REGDUMP
{
    REGISTERCONTEXT regcontext;
    FLAGS flags;
    X87FPUREGISTER x87FPURegisters[8] = {};
    uint64_t mmx[8] = {};
    MXCSRFIELDS MxCsrFields;
    X87STATUSWORDFIELDS x87StatusWordFields;
    X87CONTROLWORDFIELDS x87ControlWordFields;
    LASTERROR lastError;
    LASTSTATUS lastStatus;
};

struct REGDUMP_AVX512
{
    REGISTERCONTEXT_AVX512 regcontext;
    uint32_t lastError = 0;
    uint32_t lastStatus = 0;
};
