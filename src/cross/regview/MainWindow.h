#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <QMainWindow>

#include <MachBug/api/machbug_api.h>

#include <vector>

#include "BreakpointTable.h"
#include "MemoryMapTable.h"
#include "ModuleTable.h"
#include "ThreadTable.h"

class Architecture;
class EngineMemoryPage;
class HexDump;
class QComboBox;
class QLineEdit;
class QSpinBox;
class RegisterTable;

// Milestone 3's proof: a real process's registers and memory on screen, driven through the
// engine vtable and nothing else.
//
// This is a sample app in the shape of milestone 1's four, not the debugger. The debugger's own
// window needs disassembly, breakpoints and modules -- milestones 4 to 6 -- so it stays
// linux-only until those exist, and this app is what proves the engine meanwhile. It is also the
// bench milestone 4 will use by hand when breakpoints arrive.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Launches immediately, for the command line and for the accessibility check that drives
    // this app without a mouse.
    void launch(const QString& path);

    // Reports what the register table and the memory panel are actually showing, and whether
    // that is a populated view or an empty one. Exists because a screenshot proves this to a
    // person and nothing to CI: --selftest runs the whole app offscreen, stops a real target,
    // and fails loudly if the rows came back empty or zeroed. `report` receives one line per
    // register row plus the memory line, for a log a failure can be read out of.
    bool selfTest(QStringList* report) const;

    // Milestone 4's half of the same idea, and the only part of the breakpoint bench a job can
    // check: set a breakpoint on the address the target published, resume, and report whether
    // the target actually stopped there with the list showing it armed. Drives the Qt event
    // loop itself, because what is being checked is the view's state after the engine's
    // callbacks have been delivered -- see main.cpp's polling loop for the same reason.
    //
    // Separate from selfTest() rather than folded into it: this needs a target that publishes
    // an address (known_function), and a check that quietly skips itself when pointed at
    // anything else is a check that passes for the wrong reason.
    bool breakpointSelfTest(QStringList* report);

    // Waits for the target's first stop, resumes it briefly, and stops it again -- which
    // --selftest needs before it can assert anything about modules: at the first stop dyld has
    // published no image list, so the module table is legitimately empty and every memory
    // region reports no module. Drives the event loop itself, like breakpointSelfTest().
    bool letTheTargetRun();

    // Launches with the target's stdout captured to a temporary file, so the address a fixture
    // publishes can be read back. Only breakpointSelfTest() needs this; an ordinary launch
    // leaves the target's output where the user can see it.
    void launchCapturingOutput(const QString& path);

signals:
    // Emitted from the engine's loop thread; every connection to these is queued, which is what
    // moves the work onto the UI thread. Qt cannot marshal what it cannot copy, so these carry
    // plain values rather than pointers into engine state.
    void targetCreated(qint64 pid);
    void targetStopped();
    void targetExited(int exitCode);
    void engineError(const QString& error);
    void breakpointHit(quint64 address);

private slots:
    void onStopped();
    void onBreakpointHit(quint64 address);
    void onAddBreakpoint();
    void onRemoveBreakpoint();
    void onToggleBreakpoint(uint64_t address);
    void onRegisterEdited(const QString& name, uint64_t value);
    void onContinue();
    void onStepInto();
    void onPause();
    void onStop();
    void onLaunch();
    void onAttach();

private:
    void refreshRegisters();
    void refreshMemory();
    void refreshBreakpoints();

    // Modules, regions and threads, all three from the vtable at every stop. Separate from the
    // register refresh because they answer at different moments: at the target's first stop
    // dyld has published no images, so the module list and every region's module are empty
    // there and fill in at the next one.
    void refreshInventory();

    // The program counter to point the memory panel at: the stopped thread's when there is
    // one, and otherwise the first thread's. A Pause has no stopped thread -- it is a
    // task_suspend with no exception behind it -- so GetRegisters(threadId 0) fails there, and
    // without a fallback the panel keeps whatever address the first stop left it at.
    uint64_t mProgramCounter = 0;
    void stopEngine();
    // One place that creates the engine and starts its loop, for launch and attach alike:
    // `attachPid` of 0 means launch `path`. Two copies of this block drifted apart the moment
    // milestone 4 added a callback, which is why there is now one.
    void startEngine(const QString& path, int attachPid, const QString& description);

    DbgEngine* mEngine = nullptr;
    std::thread mStartThread;
    std::atomic<bool> mStartReturned{false};

    RegisterTable* mRegisters = nullptr;
    BreakpointTable* mBreakpointList = nullptr;
    ModuleTable* mModules = nullptr;
    MemoryMapTable* mMemoryMap = nullptr;
    ThreadTable* mThreads = nullptr;
    QLineEdit* mBreakpointAddress = nullptr;
    QComboBox* mBreakpointKind = nullptr;
    QSpinBox* mBreakpointSize = nullptr;

    // What this window asked the engine for. The vtable has no enumeration -- the caller is the
    // one that set them -- so this is the list, and the engine is asked per address whether the
    // target is carrying each one.
    std::vector<BreakpointTable::Row> mBreakpointRows;

    // The target's stdout, captured for breakpointSelfTest(); empty for an ordinary launch.
    QString mCapturedOutputPath;
    int mSavedStdout = -1;
    HexDump* mMemory = nullptr;
    EngineMemoryPage* mMemoryPage = nullptr;
    Architecture* mArchitecture = nullptr;

    bool mDescriptorsSet = false;
};
