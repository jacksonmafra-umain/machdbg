
#include <atomic>
#include <QGuiApplication>
#include <QClipboard>
#include <QThread>
#include <QCoreApplication>

#include "Bridge.h"
#include "Types.h"
#include <Disassembler/QCapstone.h>
#include <CapstoneVersion.h>
#include <Disassembler/CapstoneTokenizer.h>   // MAX_DISASM_BUFFER
#include <capstone/arm64.h>
#include <capstone/x86.h>

struct InvalidMemoryProvider : MemoryProvider
{
    bool read(duint addr, void* dest, duint size) override
    {
        return false;
    }

    bool getRange(duint addr, duint & base, duint & size) override
    {
        return false;
    }

    bool isCodePtr(duint addr) override
    {
        return false;
    }

    bool isValidPtr(duint addr) override
    {
        return false;
    }
} gInvalidMemoryProvider;

static std::atomic<MemoryProvider*> gMemory{&gInvalidMemoryProvider};

void DbgSetMemoryProvider(MemoryProvider* provider)
{
    gMemory.store(provider ? provider : &gInvalidMemoryProvider);
}

// BRIDGE

bool BridgeSettingGet(const char* section, const char* key, char* value)
{
    return false;
}

bool BridgeSettingSet(const char* section, const char* key, const char* value)
{
    return false;
}

bool BridgeSettingGetUint(const char* section, const char* key, duint* value)
{
    return false;
}

bool BridgeSettingSetUint(const char* section, const char* key, duint value)
{
    return false;
}

const wchar_t* BridgeUserDirectory()
{
    return L".";
}

void* BridgeAlloc(size_t size)
{
    return malloc(size);
}

void BridgeFree(void* ptr)
{
    free(ptr);
}

// DBG

bool DbgIsDebugging()
{
    return gMemory.load() != &gInvalidMemoryProvider;
}

DBGFUNCTIONS* DbgFunctions()
{
    static DBGFUNCTIONS f = []
    {
        DBGFUNCTIONS f{};
        f.GetTraceRecordHitCount = [](duint addr) -> duint
        {
            return 0;
        };
        f.GetTraceRecordByteType = [](duint addr)
        {
            return TRACERECORDBYTETYPE::TraceRecordByteTypeUnknown;
        };
        f.ModBaseFromAddr = [](duint addr) -> duint
        {
            duint base = 0;
            if(!gMemory.load()->modBaseFromAddr(addr, base))
                return 0;
            return base;
        };
        f.ModNameFromAddr = [](duint addr, char* name, bool extension)
        {
            return gMemory.load()->modNameFromAddr(addr, name, MAX_MODULE_SIZE, extension);
        };
        f.StringFormatInline = [](const char* format, size_t size, char* dest)
        {
            return false;
        };
        f.MemIsCodePage = [](duint addr, bool refresh)
        {
            return gMemory.load()->isCodePtr(addr);
        };
        f.GetMnemonicBrief = [](const char* mnem, size_t resultSize, char* result)
        {
            *result = '\0';
        };
        f.ModRelocationAtAddr = [](duint addr, DBGRELOCATIONINFO * relocation)
        {
            return false;
        };
        f.PatchGetEx = [](duint addr, DBGPATCHINFO * info)
        {
            return false;
        };
        f.ValFromString = [](const char* expr, duint * value)
        {
            bool success = false;
            *value = DbgEval(expr, &success);
            return success;
        };
        f.ModGetParty = [](duint addr)
        {
            return 0;
        };
        f.PatchInRange = [](duint start, duint end)
        {
            return false;
        };
        f.MemPatch = [](duint start, const unsigned char* data, duint size)
        {
            return gMemory.load()->write(start, data, size);
        };
        return f;
    }();
    return &f;
}

// TODO: resolve via per-module .dynsym lookup.
bool DbgGetLabelAt(duint addr, SEGTYPE seg, char* label)
{
    return false;
}

bool DbgGetModuleAt(duint addr, char* module)
{
    return false;
}

bool DbgGetCommentAt(duint addr, char* comment)
{
    return false;
}

// TODO: persist address colors in the cross debugger database.
bool DbgGetAddressColorAt(duint addr, unsigned int* color)
{
    return false;
}

bool DbgSetAddressColorRange(duint start, duint end, unsigned int color)
{
    return false;
}

void DbgDelAddressColorRange(duint start, duint end)
{
}

bool DbgGetBookmarkAt(duint addr)
{
    return false;
}

static std::atomic<BreakpointQueryFunc> gBreakpointQuery{nullptr};

void DbgSetBreakpointQuery(BreakpointQueryFunc func)
{
    gBreakpointQuery.store(func);
}

BPXTYPE DbgGetBpxTypeAt(duint addr)
{
    auto query = gBreakpointQuery.load();
    if(query)
        return query(addr);
    return bp_none;
}

bool DbgMemIsValidReadPtr(duint addr)
{
    return gMemory.load()->isValidPtr(addr);
}

// TODO: detect printable ASCII / UTF-16LE string at addr.
bool DbgGetStringAt(duint addr, char* str)
{
    return false;
}

duint DbgEval(const char* expr, bool* success)
{
    // TODO: replace this hex-only stub with a real cross expression evaluator (breaks rsp+8, symbols, mod.main(), etc.).
    if(success)
        *success = false;
    if(!expr)
        return 0;

    QString str = QString::fromUtf8(expr).trimmed();
    if(str.isEmpty())
        return 0;

    bool negative = false;
    if(str.startsWith('-'))
    {
        negative = true;
        str = str.mid(1).trimmed();
    }
    if(str.startsWith("0x", Qt::CaseInsensitive))
        str = str.mid(2);

    bool ok = false;
    const duint value = str.toULongLong(&ok, 16);
    if(!ok)
        return 0;

    if(success)
        *success = true;
    return negative ? static_cast<duint>(0) - value : value;
}

duint DbgValFromString(const char* expr)
{
    return DbgEval(expr, nullptr);
}

bool DbgCmdExec(const char* cmd)
{
    printf("DbgCmdExec(\"%s\")\n", cmd);
    return false;
}

bool DbgCmdExecDirect(const char* cmd)
{
    printf("DbgCmdExecDirect(\"%s\")\n", cmd);
    return false;
}

bool DbgCmdExec(const QString & cmd)
{
    return DbgCmdExec(cmd.toUtf8().constData());
}

bool DbgCmdExecDirect(const QString & cmd)
{
    return DbgCmdExecDirect(cmd.toUtf8().constData());
}

duint DbgMemFindBaseAddr(duint addr, duint* size)
{
    duint rangeBase = 0;
    duint rangeSize = 0;
    if(!gMemory.load()->getRange(addr, rangeBase, rangeSize))
        return 0;

    if(size != nullptr)
        *size = rangeSize;

    return rangeBase;
}

bool DbgMemRead(duint addr, void* dest, size_t size)
{
    return gMemory.load()->read(addr, dest, size);
}

FUNCTYPE DbgGetFunctionTypeAt(duint addr)
{
    return FUNC_NONE;
}

XREFTYPE DbgGetXrefTypeAt(duint addr)
{
    return XREF_NONE;
}

ARGTYPE DbgGetArgTypeAt(duint addr)
{
    return ARG_NONE;
}

LOOPTYPE DbgGetLoopTypeAt(duint addr, int depth)
{
    return LOOP_NONE;
}

duint DbgGetBranchDestination(duint addr)
{
    // Reimplemented on Capstone when Zydis was retired (milestone 6 task 5). The disassembly
    // view no longer needs it -- QCapstone fills Instruction_t::branchDestination from the
    // decode it already has -- but the sidebar and the jump-arrow drawing still ask, so it
    // answers rather than returning zero and making every arrow disappear.
    //
    // The architecture is the host's, which is the same TODO the Zydis version carried: nothing
    // here is told which target it is describing. It is right for a debugger running native
    // code and wrong for anything else, and the only honest fix is plumbing the architecture
    // through this interface -- a change to the bridge's own shape, not to this function.
    uint8_t data[MAX_DISASM_BUFFER];
    if(!DbgMemRead(addr, data, sizeof(data)))
        return 0;

#if defined(__arm64__) || defined(__aarch64__)
    static const cs_arch arch = static_cast<cs_arch>(MACHDBG_CS_ARCH_ARM64);
    static const cs_mode mode = CS_MODE_LITTLE_ENDIAN;
#else
    static const cs_arch arch = CS_ARCH_X86;
    static const cs_mode mode = CS_MODE_64;
#endif

    csh handle = 0;
    if(cs_open(arch, mode, &handle) != CS_ERR_OK)
        return 0;
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    duint destination = 0;
    cs_insn* insn = nullptr;
    if(cs_disasm(handle, data, sizeof(data), addr, 1, &insn) == 1 && insn->detail != nullptr)
    {
        // The operand type, not CS_GRP_BRANCH_RELATIVE: that group is reported for indirect
        // branches too (measured in milestone 6 task 4), so it does not mean the destination is
        // knowable. An immediate operand is an address; a register operand is a value this code
        // cannot resolve without running the target.
#if defined(__arm64__) || defined(__aarch64__)
        const cs_arm64& detail = insn->detail->arm64;
        if(detail.op_count > 0 && detail.operands[0].type == ARM64_OP_IMM)
            destination = static_cast<duint>(detail.operands[0].imm);
#else
        const cs_x86& detail = insn->detail->x86;
        if(detail.op_count > 0 && detail.operands[0].type == X86_OP_IMM)
            destination = static_cast<duint>(detail.operands[0].imm);
#endif
        cs_free(insn, 1);
    }

    cs_close(&handle);
    return destination;
}

bool DbgIsJumpGoingToExecute(duint addr)
{
    return false; // TODO
}

bool DbgXrefGet(duint addr, XREF_INFO* info)
{
    return false;
}

void DbgReleaseEncodeTypeBuffer(void* buffer)
{
    BridgeFree(buffer);
}

void* DbgGetEncodeTypeBuffer(duint addr, duint* size)
{
    return nullptr;
}

bool DbgSetEncodeType(duint addr, duint size, ENCODETYPE type)
{
    return false;
}

void DbgDelEncodeTypeRange(duint start, duint end)
{
}

void DbgDelEncodeTypeSegment(duint start)
{
}

// GUI

void GuiExecuteOnGuiThreadEx(GuiCallback callback, void* data)
{
    if(QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        callback(data);
        return;
    }
    QMetaObject::invokeMethod(QCoreApplication::instance(), [callback, data]()
    {
        callback(data);
    }, Qt::QueuedConnection);
}

void GuiAddLogMessage(const char* msg)
{
    Bridge::getBridge()->addMsgToLog(msg);
}

void GuiUpdateAllViews()
{
    Bridge::getBridge()->repaintTableView();
}

void GuiUpdatePatches()
{
}

Bridge* Bridge::getBridge()
{
    static Bridge i;
    return &i;
}

void Bridge::CopyToClipboard(const QString & str)
{
    QGuiApplication::clipboard()->setText(str);
}

void Bridge::addMsgToLog(const QByteArray & bytes)
{
    printf("addMsgToLog: %s\n", bytes.data());
}
