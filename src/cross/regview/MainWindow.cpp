#include "MainWindow.h"

#include <chrono>
#include <functional>
#include <vector>
#include <thread>

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QThread>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QSplitter>
#include <QStatusBar>
#include <QRegularExpression>
#include <QToolBar>

#include <BasicView/HexDump.h>
#include <Configuration.h>
#include <Disassembler/Architecture.h>

#include <cstdio>
#include <unistd.h>

#include "BreakpointTable.h"
#include "MemoryMapTable.h"
#include "ModuleTable.h"
#include "ThreadTable.h"
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

    void onBreakpoint(uint64_t address, void* userdata)
    {
        auto* window = static_cast<MainWindow*>(userdata);
        emit window->breakpointHit(address);
        emit window->targetStopped();
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

    mBreakpointList = new BreakpointTable(this);

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

    // The bench: an address, a kind, a size for the watchpoint kinds, and the list. Everything
    // here goes through the vtable, exactly as the register table does -- this window has no
    // other way to reach the engine and is not given one.
    mBreakpointAddress = new QLineEdit(this);
    mBreakpointAddress->setPlaceholderText(tr("address in hex"));
    mBreakpointAddress->setAccessibleName(tr("Breakpoint address"));

    mBreakpointKind = new QComboBox(this);
    mBreakpointKind->setAccessibleName(tr("Breakpoint kind"));
    mBreakpointKind->addItem(BreakpointTable::kindName(DbgBreakpointKind_Software),
                             DbgBreakpointKind_Software);
    mBreakpointKind->addItem(BreakpointTable::kindName(DbgBreakpointKind_HwExec),
                             DbgBreakpointKind_HwExec);
    mBreakpointKind->addItem(BreakpointTable::kindName(DbgBreakpointKind_HwRead),
                             DbgBreakpointKind_HwRead);
    mBreakpointKind->addItem(BreakpointTable::kindName(DbgBreakpointKind_HwWrite),
                             DbgBreakpointKind_HwWrite);

    mBreakpointSize = new QSpinBox(this);
    mBreakpointSize->setAccessibleName(tr("Watchpoint size"));
    // The sizes the hardware can encode, and no others -- the engine refuses the rest by name
    // (see arch::WatchpointFits), and offering a three in a spin box would be an invitation to
    // find that out the hard way.
    mBreakpointSize->setRange(1, 8);
    mBreakpointSize->setValue(8);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Address:"), this));
    controls->addWidget(mBreakpointAddress, 1);
    controls->addWidget(new QLabel(tr("Kind:"), this));
    controls->addWidget(mBreakpointKind);
    controls->addWidget(new QLabel(tr("Size:"), this));
    controls->addWidget(mBreakpointSize);
    auto* addButton = new QPushButton(tr("Add"), this);
    auto* removeButton = new QPushButton(tr("Remove"), this);
    controls->addWidget(addButton);
    controls->addWidget(removeButton);

    auto* bench = new QWidget(this);
    auto* benchLayout = new QVBoxLayout(bench);
    benchLayout->setContentsMargins(0, 0, 0, 0);
    benchLayout->addLayout(controls);
    benchLayout->addWidget(mBreakpointList, 1);

    mModules = new ModuleTable(this);
    mMemoryMap = new MemoryMapTable(this);
    mThreads = new ThreadTable(this);

    // Tabbed, not tiled: four tables and a hex dump do not fit side by side, and milestone 3's
    // registers and memory keep the room they were given.
    auto* inventory = new QTabWidget(this);
    inventory->setAccessibleName(tr("Inventory"));
    inventory->addTab(bench, tr("Breakpoints"));
    inventory->addTab(mModules, tr("Modules"));
    inventory->addTab(mMemoryMap, tr("Memory map"));
    inventory->addTab(mThreads, tr("Threads"));

    auto* vertical = new QSplitter(Qt::Vertical, this);
    vertical->addWidget(splitter);
    vertical->addWidget(inventory);
    inventory->setMinimumHeight(160);
    vertical->setSizes({420, 200});
    vertical->setStretchFactor(0, 1);
    setCentralWidget(vertical);

    connect(addButton, &QPushButton::clicked, this, &MainWindow::onAddBreakpoint);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::onRemoveBreakpoint);
    connect(mBreakpointList, &BreakpointTable::enabledToggleRequested,
            this, &MainWindow::onToggleBreakpoint);

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
    connect(this, &MainWindow::breakpointHit, this, &MainWindow::onBreakpointHit,
            Qt::QueuedConnection);
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
    startEngine(path, 0, tr("Launching %1...").arg(path));
}

void MainWindow::launchCapturingOutput(const QString& path)
{
    // The fixture inherits this process's stdout at spawn time, so redirecting it here is how
    // its published address is read back. Restored by breakpointSelfTest() before anything is
    // printed -- the report has to reach the real stdout, and Qt's own warnings landing in the
    // capture file would sit in front of the line being parsed.
    QTemporaryFile capture;
    capture.setAutoRemove(false);
    if(!capture.open())
    {
        statusBar()->showMessage(tr("Could not capture the target's output"));
        return;
    }
    mCapturedOutputPath = capture.fileName();
    capture.close();

    std::fflush(stdout);
    mSavedStdout = dup(STDOUT_FILENO);
    FILE* redirected = std::freopen(mCapturedOutputPath.toUtf8().constData(), "w", stdout);
    if(redirected == nullptr || mSavedStdout == -1)
    {
        statusBar()->showMessage(tr("Could not redirect the target's output"));
        return;
    }

    launch(path);
}

void MainWindow::startEngine(const QString& path, const int attachPid, const QString& description)
{
    stopEngine();

    DbgEngineCallbacks callbacks{};
    callbacks.onCreateProcess = onCreateProcess;
    callbacks.onSystemBreakpoint = onSystemBreakpoint;
    callbacks.onStep = onStep;
    callbacks.onBreakpoint = onBreakpoint;
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

    // The breakpoints belonged to the previous target; the new one carries none of them.
    mBreakpointRows.clear();
    mBreakpointList->setBreakpoints(mBreakpointRows);

    // Start() owns the exception loop for the life of the target (machbug_api.h), so it cannot
    // run on the UI thread -- the window would never paint again. The path is copied into the
    // thread rather than pointed at: DbgLaunchSpec holds a borrowed pointer, and a QString on
    // this stack would be gone by the time Start() reads it.
    const std::string target = path.toStdString();
    mStartThread = std::thread([this, target, attachPid] {
        DbgLaunchSpec spec{};
        if(attachPid != 0)
            spec.attachPid = attachPid;
        else
            spec.path = target.c_str();
        mEngine->Start(mEngine->impl, &spec);
        mStartReturned.store(true);
    });

    statusBar()->showMessage(description);
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

    // The three tables milestone 5 added. Checked for content rather than for rows: a module
    // list with forty-five empty rows renders exactly like a populated one, and only the values
    // tell them apart.
    //
    // The target has to have run first. At its very first stop dyld has published no image
    // list, so the module table is legitimately empty there and every region reports no module
    // -- which is why this is checked here, from a self-test that resumes the target, rather
    // than asserted at the first stop where it would be a lie about the engine.
    const QStringList moduleRows = mModules->rows();
    const QStringList mapRows = mMemoryMap->rows();
    const QStringList threadRows = mThreads->rows();

    bool aModuleHasASlide = false;
    for(const QString& row : moduleRows)
    {
        // "<name> <base> <slide> <path>", and a slide of 0x0 everywhere is what a list built
        // from a stub that forgot to subtract looks like.
        const QStringList parts = row.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if(parts.size() >= 3 && parts.at(2) != QStringLiteral("0x0"))
            aModuleHasASlide = true;
    }

    bool aRegionNamesAModule = false;
    for(const QString& row : mapRows)
    {
        if(row.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 5)
            aRegionNamesAModule = true;   // the module column is the fifth and is blank when empty
    }

    bool aThreadHasAProgramCounter = false;
    for(const QString& row : threadRows)
    {
        const QStringList parts = row.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for(const QString& part : parts)
        {
            if(part.startsWith(QStringLiteral("0x")) && part != QStringLiteral("0x0"))
                aThreadHasAProgramCounter = true;
        }
    }

    if(report)
    {
        report->append(QStringLiteral("modules %1 row(s), a slide that is not zero: %2")
            .arg(moduleRows.size())
            .arg(aModuleHasASlide ? QStringLiteral("yes") : QStringLiteral("no")));
        report->append(QStringLiteral("memory map %1 row(s), a region naming a module: %2")
            .arg(mapRows.size())
            .arg(aRegionNamesAModule ? QStringLiteral("yes") : QStringLiteral("no")));
        report->append(QStringLiteral("threads %1 row(s), a readable program counter: %2")
            .arg(threadRows.size())
            .arg(aThreadHasAProgramCounter ? QStringLiteral("yes") : QStringLiteral("no")));
        for(const QString& row : threadRows)
            report->append(QStringLiteral("  thread row: ") + row);
    }

    return pcPopulated && spPopulated && memoryReadable &&
           aModuleHasASlide && aRegionNamesAModule && aThreadHasAProgramCounter;
}

bool MainWindow::letTheTargetRun()
{
    if(!mEngine)
        return false;

    const auto waitFor = [](const std::function<bool()>& done) {
        for(int attempt = 0; attempt < 100 && !done(); ++attempt)
        {
            QApplication::processEvents();
            if(!done())
                QThread::msleep(50);
        }
        return done();
    };

    // The first stop, which is where a launch leaves the target: the register table having rows
    // is how this window knows it happened.
    if(!waitFor([this] { return mRegisters->getRowCount() > 0; }))
        return false;

    onContinue();
    QApplication::processEvents();
    QThread::msleep(250);
    onPause();
    QApplication::processEvents();

    // Pausing runs the same refresh a stop does, so the module table having rows is this
    // window's evidence that the target both ran and came back -- and it is the exact thing
    // the caller is about to assert on, read here rather than assumed.
    return !mModules->rows().isEmpty();
}

bool MainWindow::breakpointSelfTest(QStringList* report)
{
    const auto say = [report](const QString& line) {
        if(report)
            report->append(line);
    };

    if(!mEngine)
    {
        say(QStringLiteral("no engine: nothing was launched"));
        return false;
    }

    // Every wait below drives the event loop rather than sleeping through it: the engine's
    // callbacks reach this window as queued signals, so a wait that does not process events is
    // a wait for something that cannot arrive. Five seconds each, which is far longer than a
    // fixture takes and short enough to fail rather than hang a job.
    const auto waitFor = [this](const std::function<bool()>& done) {
        for(int attempt = 0; attempt < 100 && !done(); ++attempt)
        {
            QApplication::processEvents();
            if(!done())
                QThread::msleep(50);
        }
        return done();
    };

    // The fixture prints its function address once, at the top of main, so the target has to
    // reach main first -- which means answering its first stop.
    if(!waitFor([this] { return mRegisters->getRowCount() > 0; }))
    {
        say(QStringLiteral("the target never reached its first stop"));
        return false;
    }
    mEngine->Continue(mEngine->impl);

    uint64_t functionAddress = 0;
    waitFor([this, &functionAddress] {
        if(FILE* captured = std::fopen(mCapturedOutputPath.toUtf8().constData(), "r"))
        {
            unsigned long long printed = 0;
            if(std::fscanf(captured, "%llx", &printed) == 1)
                functionAddress = printed;
            std::fclose(captured);
        }
        return functionAddress != 0;
    });

    // Restored before anything is reported: the report belongs on the real stdout, and from
    // here on nothing more is read out of the capture.
    std::fflush(stdout);
    if(mSavedStdout != -1)
    {
        dup2(mSavedStdout, STDOUT_FILENO);
        ::close(mSavedStdout);
        mSavedStdout = -1;
    }
    if(!mCapturedOutputPath.isEmpty())
    {
        unlink(mCapturedOutputPath.toUtf8().constData());
        mCapturedOutputPath.clear();
    }

    if(functionAddress == 0)
    {
        say(QStringLiteral("the target published no address to set a breakpoint on"));
        return false;
    }
    say(QStringLiteral("the target published its function at 0x%1").arg(functionAddress, 0, 16));

    if(mEngine->Pause(mEngine->impl) != DbgStatus_Ok)
    {
        say(QStringLiteral("could not pause the target: %1")
                .arg(QString::fromUtf8(DbgLastErrorString())));
        return false;
    }

    mBreakpointAddress->setText(QStringLiteral("%1").arg(functionAddress, 0, 16));
    mBreakpointKind->setCurrentIndex(0);   // software
    onAddBreakpoint();

    const QStringList afterAdd = mBreakpointList->rows();
    *report += afterAdd;
    if(afterAdd.isEmpty() || !afterAdd.first().contains(QStringLiteral("armed")))
    {
        say(QStringLiteral("the breakpoint was not armed: %1")
                .arg(QString::fromUtf8(DbgLastErrorString())));
        return false;
    }

    // Resumed, and the target has to come back on its own. The fixture calls the function every
    // fifty milliseconds, so a breakpoint that is really in the target stops it almost at once
    // -- and one that is only in this list never does, which is the failure this exists to
    // catch.
    const QString before = statusBar()->currentMessage();
    mEngine->Continue(mEngine->impl);
    const bool stopped = waitFor([this, functionAddress] {
        return statusBar()->currentMessage().contains(
            QStringLiteral("0x%1").arg(functionAddress, 0, 16));
    });

    say(QStringLiteral("status before the resume: %1").arg(before));
    say(QStringLiteral("status after the resume: %1").arg(statusBar()->currentMessage()));
    *report += mBreakpointList->rows();
    if(!stopped)
    {
        say(QStringLiteral("the target did not stop on the breakpoint"));
        return false;
    }

    // And the list still says the target is carrying it, read back through the engine rather
    // than from what this window remembers asking for.
    refreshBreakpoints();
    const QStringList afterHit = mBreakpointList->rows();
    return !afterHit.isEmpty() && afterHit.first().contains(QStringLiteral("armed"));
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

    startEngine(QString(), pid, tr("Attaching to %1...").arg(pid));
}

void MainWindow::onStopped()
{
    refreshRegisters();
    refreshMemory();
    refreshBreakpoints();
    refreshInventory();
    // Not overwritten when a breakpoint stop has already said something more specific: "Stopped"
    // on top of "Stopped on a hardware write watchpoint" would throw away the only part of the
    // message the user could not have worked out for themselves.
    if(statusBar()->currentMessage().isEmpty() ||
       !statusBar()->currentMessage().startsWith(tr("Stopped on")))
        statusBar()->showMessage(tr("Stopped"));
}

void MainWindow::onBreakpointHit(const quint64 address)
{
    DbgBreakpointKind kind = DbgBreakpointKind_Software;
    for(const BreakpointTable::Row& row : mBreakpointRows)
    {
        if(row.address == address)
            kind = row.kind;
    }

    // A watchpoint's address is the data it watched, not the instruction that touched it, and
    // saying so is the difference between a status bar that informs and one that misleads.
    const bool watchpoint =
        kind == DbgBreakpointKind_HwRead || kind == DbgBreakpointKind_HwWrite;
    statusBar()->showMessage(watchpoint
        ? tr("Stopped on a %1 watchpoint, data address 0x%2")
              .arg(kind == DbgBreakpointKind_HwRead ? tr("read") : tr("write"))
              .arg(address, 0, 16)
        : tr("Stopped on a %1 breakpoint at 0x%2")
              .arg(BreakpointTable::kindName(kind)).arg(address, 0, 16));
}

void MainWindow::onAddBreakpoint()
{
    if(!mEngine)
    {
        statusBar()->showMessage(tr("Launch or attach to a target first"));
        return;
    }

    bool parsed = false;
    const uint64_t address =
        mBreakpointAddress->text().trimmed().remove(QStringLiteral("0x")).toULongLong(&parsed, 16);
    if(!parsed || address == 0)
    {
        statusBar()->showMessage(tr("That is not an address"));
        return;
    }

    const auto kind =
        static_cast<DbgBreakpointKind>(mBreakpointKind->currentData().toInt());
    const bool watchpoint =
        kind == DbgBreakpointKind_HwRead || kind == DbgBreakpointKind_HwWrite;
    const uint32_t size = watchpoint ? static_cast<uint32_t>(mBreakpointSize->value()) : 0;

    if(mEngine->SetBreakpoint(mEngine->impl, kind, address, size) != DbgStatus_Ok)
    {
        // The engine's own words, not a rewrite of them: "all 6 execution slots are in use" and
        // "sizes of 1, 2, 4 and 8 bytes only" are what tell the user what to do instead.
        statusBar()->showMessage(QString::fromUtf8(DbgLastErrorString()));
        return;
    }

    BreakpointTable::Row row;
    row.address = address;
    row.kind = kind;
    row.size = size;
    mBreakpointRows.push_back(row);
    refreshBreakpoints();
}

void MainWindow::onRemoveBreakpoint()
{
    if(!mEngine)
        return;

    const uint64_t address = mBreakpointList->selectedAddress();
    if(address == 0)
    {
        statusBar()->showMessage(tr("Select a breakpoint to remove"));
        return;
    }

    if(mEngine->DeleteBreakpoint(mEngine->impl, address) != DbgStatus_Ok)
    {
        statusBar()->showMessage(QString::fromUtf8(DbgLastErrorString()));
        return;
    }

    for(auto it = mBreakpointRows.begin(); it != mBreakpointRows.end(); ++it)
    {
        if(it->address == address)
        {
            mBreakpointRows.erase(it);
            break;
        }
    }
    refreshBreakpoints();
}

void MainWindow::onToggleBreakpoint(const uint64_t address)
{
    if(!mEngine)
        return;

    for(BreakpointTable::Row& row : mBreakpointRows)
    {
        if(row.address != address)
            continue;

        if(mEngine->SetBreakpointEnabled(mEngine->impl, address, !row.enabled) != DbgStatus_Ok)
        {
            statusBar()->showMessage(QString::fromUtf8(DbgLastErrorString()));
            return;
        }
        // Only after the engine agreed: a checkbox that moves before the target does is a view
        // that lies about what is in the process.
        row.enabled = !row.enabled;
        break;
    }
    refreshBreakpoints();
}

void MainWindow::refreshInventory()
{
    if(!mEngine)
        return;

    // Each of these grows until the engine stops filling the buffer. Getting `capacity` back
    // means "there may be more", which the contract says outright -- and a map that stopped at
    // the first guess showed exactly 1024 regions of a process that has more, a number that
    // looks like a fact and is really the size of an array.
    constexpr uint32_t kMostRowsWorthAsking = 1u << 16;

    std::vector<DbgModule> modules;
    for(uint32_t capacity = 256; capacity <= kMostRowsWorthAsking; capacity *= 2)
    {
        modules.assign(capacity, DbgModule{});
        uint32_t count = 0;
        if(mEngine->ModEnum(mEngine->impl, modules.data(), capacity, &count) != DbgStatus_Ok)
        {
            modules.clear();
            break;
        }
        modules.resize(count);
        if(count < capacity)
            break;
    }
    mModules->setModules(modules);

    std::vector<DbgMemoryRegion> regions;
    for(uint32_t capacity = 1024; capacity <= kMostRowsWorthAsking; capacity *= 2)
    {
        regions.assign(capacity, DbgMemoryRegion{});
        uint32_t count = 0;
        if(mEngine->MemEnumRegions(mEngine->impl, regions.data(), capacity, &count) !=
           DbgStatus_Ok)
        {
            regions.clear();
            break;
        }
        regions.resize(count);
        if(count < capacity)
            break;
    }
    mMemoryMap->setRegions(regions, modules);

    std::vector<DbgThread> threads;
    for(uint32_t capacity = 64; capacity <= kMostRowsWorthAsking; capacity *= 2)
    {
        threads.assign(capacity, DbgThread{});
        uint32_t count = 0;
        if(mEngine->ThreadEnum(mEngine->impl, threads.data(), capacity, &count) != DbgStatus_Ok)
        {
            threads.clear();
            break;
        }
        threads.resize(count);
        if(count < capacity)
            break;
    }
    mThreads->setThreads(threads);
}

void MainWindow::refreshBreakpoints()
{
    for(BreakpointTable::Row& row : mBreakpointRows)
    {
        row.effective = mEngine != nullptr &&
                        mEngine->IsBreakpointEffective(mEngine->impl, row.address);
    }
    mBreakpointList->setBreakpoints(mBreakpointRows);
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
