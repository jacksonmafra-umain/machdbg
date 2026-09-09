#include "MainWindow.h"

#include <chrono>
#include <thread>

#include <QFileDialog>
#include <QInputDialog>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QRegularExpression>
#include <QToolBar>

#include <BasicView/HexDump.h>
#include <Configuration.h>
#include <Disassembler/Architecture.h>

#include "EngineMemoryPage.h"
#include "RegisterTable.h"

namespace
{
    // The same shape hex_viewer uses: this app shows 64-bit targets only, which is every target
    // milestone 3 supports (i386 has no register file in the engine).
    struct DefaultArchitecture : Architecture
    {
        bool disasm64() const override { return true; }
        bool addr64() const override { return true; }
    } gArchitecture;

    // The callbacks arrive on the engine's loop thread. They do nothing but emit, because
    // everything they would otherwise touch belongs to the UI thread -- the connections in the
    // constructor are queued, which is what performs the handoff.
    void onCreateProcess(pid_t pid, uint64_t /*entryPoint*/, void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->targetCreated(pid);
    }

    void onSystemBreakpoint(void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->targetStopped();
    }

    void onStep(void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->targetStopped();
    }

    void onException(uint32_t /*type*/, uint64_t /*address*/, void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->targetStopped();
    }

    void onExitProcess(int exitCode, void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->targetExited(exitCode);
    }

    void onError(const char* error, void* userdata)
    {
        emit static_cast<MainWindow*>(userdata)->engineError(
            QString::fromUtf8(error ? error : "(null)"));
    }
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mArchitecture(&gArchitecture)
{
    setWindowTitle(tr("machdbg registers"));
    resize(1000, 640);

    mRegisters = new RegisterTable(this);
    // Deliberately parentless: HexDump::~HexDump() does `delete mMemPage`, so the page it is
    // handed becomes its property. Giving it a QObject parent as well makes Qt delete it too,
    // which crashed this app in EXC_BAD_ACCESS inside ~HexDump() before this comment existed.
    // mMemoryPage stays valid for as long as mMemory does, and ~MainWindow's body (stopEngine())
    // runs before its children are destroyed, so clearing the engine there is still safe.
    mMemoryPage = new EngineMemoryPage();
    mMemory = new HexDump(mArchitecture, this, mMemoryPage);
    mMemory->setAccessibleName(tr("Memory"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(mRegisters);
    splitter->addWidget(mMemory);
    // A minimum width and explicit sizes, not just a stretch factor: an AbstractTableView's size
    // hint does not reserve room for its columns, so a stretch factor alone collapsed the
    // register table to nothing -- and a table with no width has no visible rows, which is what
    // an accessibility client reads it as. It rendered "correctly" while showing nothing.
    mRegisters->setMinimumWidth(320);
    splitter->setSizes({340, 660});
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    auto* toolBar = addToolBar(tr("Debug"));
    toolBar->addAction(tr("Launch..."), this, &MainWindow::onLaunch);
    toolBar->addAction(tr("Attach..."), this, &MainWindow::onAttach);
    toolBar->addSeparator();
    toolBar->addAction(tr("Continue"), this, &MainWindow::onContinue);
    toolBar->addAction(tr("Step"), this, &MainWindow::onStepInto);
    toolBar->addAction(tr("Pause"), this, &MainWindow::onPause);
    toolBar->addAction(tr("Stop"), this, &MainWindow::onStop);

    connect(mRegisters, &RegisterTable::registerEdited, this, &MainWindow::onRegisterEdited);

    // Queued, every one of them: the sender is the engine's loop thread and the receiver touches
    // widgets. A direct connection here would paint from the wrong thread, which Qt does not
    // always catch and does not always survive.
    connect(this, &MainWindow::targetStopped, this, &MainWindow::onStopped, Qt::QueuedConnection);
    connect(this, &MainWindow::targetCreated, this, [this](qint64 pid) {
        statusBar()->showMessage(tr("Target %1 created").arg(pid));
    }, Qt::QueuedConnection);
    connect(this, &MainWindow::targetExited, this, [this](int exitCode) {
        statusBar()->showMessage(tr("Target exited with %1").arg(exitCode));
    }, Qt::QueuedConnection);
    connect(this, &MainWindow::engineError, this, [this](const QString& error) {
        statusBar()->showMessage(error);
    }, Qt::QueuedConnection);

    statusBar()->showMessage(tr("Launch or attach to a target"));
}

MainWindow::~MainWindow()
{
    stopEngine();
}

void MainWindow::launch(const QString& path)
{
    stopEngine();

    DbgEngineCallbacks callbacks{};
    callbacks.onCreateProcess = onCreateProcess;
    callbacks.onSystemBreakpoint = onSystemBreakpoint;
    callbacks.onStep = onStep;
    callbacks.onException = onException;
    callbacks.onExitProcess = onExitProcess;
    callbacks.onError = onError;
    callbacks.userdata = this;

    mEngine = MachBugCreate(&callbacks);
    if(!mEngine)
    {
        statusBar()->showMessage(tr("The engine could not be created"));
        return;
    }
    mMemoryPage->setEngine(mEngine);
    mDescriptorsSet = false;
    mStartReturned.store(false);

    // Start() owns the exception loop for the life of the target (machbug_api.h), so it cannot
    // run on the UI thread -- the window would never paint again.
    const std::string target = path.toStdString();
    mStartThread = std::thread([this, target] {
        DbgLaunchSpec spec{};
        spec.path = target.c_str();
        mEngine->Start(mEngine->impl, &spec);
        mStartReturned.store(true);
    });

    statusBar()->showMessage(tr("Launching %1...").arg(path));
}

bool MainWindow::selfTest(QStringList* report) const
{
    if(!mEngine)
    {
        if(report)
            report->append(QStringLiteral("no engine: nothing was launched"));
        return false;
    }

    const QStringList rows = mRegisters->rows();
    if(report)
        *report += rows;

    if(rows.isEmpty())
    {
        if(report)
            report->append(QStringLiteral("the register table has no rows"));
        return false;
    }

    // The program counter and the stack pointer specifically: a table of 34 rows of
    // 0000000000000000 is exactly what a view that renders but is never fed looks like, and it
    // would pass a check that only counted rows.
    const DbgArch arch = mEngine->GetArch(mEngine->impl);
    const QString pcName = arch == DbgArch_Arm64 ? QStringLiteral("pc") : QStringLiteral("rip");
    const QString spName = arch == DbgArch_Arm64 ? QStringLiteral("sp") : QStringLiteral("rsp");

    bool pcPopulated = false;
    bool spPopulated = false;
    for(const QString& row : rows)
    {
        const QStringList parts = row.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if(parts.size() < 2)
            continue;
        const bool nonZero = parts.at(1).contains(QRegularExpression(QStringLiteral("[1-9a-f]")));
        if(parts.at(0) == pcName)
            pcPopulated = nonZero;
        if(parts.at(0) == spName)
            spPopulated = nonZero;
    }

    // And the memory panel: one byte read through the page the view is pointed at proves the
    // dump has something real behind it rather than an empty window.
    uint8_t byte = 0;
    const bool memoryReadable = mMemoryPage->getSize() > 0 &&
                                mMemoryPage->read(&byte, 0, sizeof(byte));
    if(report)
    {
        // Geometry and row counts, because "the table is empty" has two very different causes --
        // no data, or no room to show it -- and an accessibility client cannot tell them apart:
        // it reads the *visible* row count, so a table with data and no height looks identical
        // to one with height and no data.
        report->append(QStringLiteral("registers widget %1x%2, %3 model row(s), %4 viewable")
            .arg(mRegisters->width()).arg(mRegisters->height())
            .arg(mRegisters->getRowCount()).arg(mRegisters->getViewableRowsCount()));
        report->append(QStringLiteral("memory widget %1x%2, %3 viewable")
            .arg(mMemory->width()).arg(mMemory->height()).arg(mMemory->getViewableRowsCount()));
        report->append(QStringLiteral("memory base 0x%1 size 0x%2 readable %3")
            .arg(mMemoryPage->getBase(), 0, 16)
            .arg(mMemoryPage->getSize(), 0, 16)
            .arg(memoryReadable ? QStringLiteral("yes") : QStringLiteral("no")));
    }

    return pcPopulated && spPopulated && memoryReadable;
}

void MainWindow::onLaunch()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Launch a target"));
    if(!path.isEmpty())
        launch(path);
}

void MainWindow::onAttach()
{
    bool accepted = false;
    const int pid = QInputDialog::getInt(this, tr("Attach"), tr("Process id"), 0, 1, 1 << 30, 1,
                                         &accepted);
    if(!accepted)
        return;

    stopEngine();

    DbgEngineCallbacks callbacks{};
    callbacks.onCreateProcess = onCreateProcess;
    callbacks.onSystemBreakpoint = onSystemBreakpoint;
    callbacks.onStep = onStep;
    callbacks.onException = onException;
    callbacks.onExitProcess = onExitProcess;
    callbacks.onError = onError;
    callbacks.userdata = this;

    mEngine = MachBugCreate(&callbacks);
    if(!mEngine)
        return;
    mMemoryPage->setEngine(mEngine);
    mDescriptorsSet = false;
    mStartReturned.store(false);

    mStartThread = std::thread([this, pid] {
        DbgLaunchSpec spec{};
        spec.attachPid = pid;
        mEngine->Start(mEngine->impl, &spec);
        mStartReturned.store(true);
    });
    statusBar()->showMessage(tr("Attaching to %1...").arg(pid));
}

void MainWindow::onStopped()
{
    refreshRegisters();
    refreshMemory();
    statusBar()->showMessage(tr("Stopped"));
}

void MainWindow::refreshRegisters()
{
    if(!mEngine)
        return;

    const DbgArch arch = mEngine->GetArch(mEngine->impl);
    if(!mDescriptorsSet)
    {
        uint32_t count = 0;
        const DbgRegisterDesc* descs = mEngine->GetRegisterDescs(mEngine->impl, arch, &count);
        mRegisters->setDescriptors(descs, count);
        // Set even when the table came back empty, so an architecture the engine has no register
        // file for does not make this retry on every stop.
        mDescriptorsSet = true;
    }

    DbgRegisters regs{};
    if(mEngine->GetRegisters(mEngine->impl, 0, &regs) != DbgStatus_Ok)
    {
        statusBar()->showMessage(QString::fromUtf8(DbgLastErrorString()));
        return;
    }
    mRegisters->setRegisters(regs);
}

void MainWindow::refreshMemory()
{
    if(!mEngine)
        return;

    DbgRegisters regs{};
    if(mEngine->GetRegisters(mEngine->impl, 0, &regs) != DbgStatus_Ok)
        return;

    const DbgArch arch = mEngine->GetArch(mEngine->impl);
    const uint64_t pc = arch == DbgArch_Arm64 ? regs.arm64.pc : regs.x86_64.rip;

    // The mapping the program counter is in, so the dump shows something real and its scrollbar
    // has a range that means something -- rather than a window around an address that may sit
    // one byte from the end of a mapping.
    uint64_t base = 0;
    uint64_t size = 0;
    if(mEngine->MemFindBaseAddr(mEngine->impl, pc, &base, &size) != DbgStatus_Ok)
        return;

    mMemoryPage->setAttributes(base, size);
    mMemory->setRowCount(size / 16);
    mMemory->setTableOffset((pc - base) / 16);
    mMemory->reloadData();
}

void MainWindow::onRegisterEdited(const QString& name, const uint64_t value)
{
    if(!mEngine)
        return;

    if(mEngine->SetRegister(mEngine->impl, 0, name.toUtf8().constData(), value) != DbgStatus_Ok)
    {
        statusBar()->showMessage(QString::fromUtf8(DbgLastErrorString()));
        return;
    }
    // Re-read rather than trusting the write: what the view shows is what the target holds, and
    // those are not the same claim.
    refreshRegisters();
}

void MainWindow::onContinue()
{
    if(mEngine)
        mEngine->Continue(mEngine->impl);
}

void MainWindow::onStepInto()
{
    if(mEngine)
        mEngine->StepInto(mEngine->impl);
}

void MainWindow::onPause()
{
    if(!mEngine)
        return;
    if(mEngine->Pause(mEngine->impl) == DbgStatus_Ok)
        onStopped();
}

void MainWindow::onStop()
{
    stopEngine();
    statusBar()->showMessage(tr("Stopped and detached"));
}

void MainWindow::stopEngine()
{
    if(!mEngine)
        return;

    mEngine->Stop(mEngine->impl);

    // Bounded, not a bare join: if Stop() ever fails to bring the loop down, joining here would
    // hang the window instead of leaving a usable app and a visible complaint. Five seconds is
    // far longer than the teardown takes when it works.
    for(int i = 0; i < 500 && !mStartReturned.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if(mStartThread.joinable())
    {
        if(mStartReturned.load())
            mStartThread.join();
        else
            mStartThread.detach();
    }

    mMemoryPage->setEngine(nullptr);
    // Destroyed only if the loop actually returned: MachBugDestroy() tears down the Debugger the
    // detached thread would still be reading from.
    if(mStartReturned.load())
        MachBugDestroy(mEngine);
    mEngine = nullptr;
}
