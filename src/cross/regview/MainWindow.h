#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <QMainWindow>

#include <MachBug/api/machbug_api.h>

class Architecture;
class EngineMemoryPage;
class HexDump;
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

signals:
    // Emitted from the engine's loop thread; every connection to these is queued, which is what
    // moves the work onto the UI thread. Qt cannot marshal what it cannot copy, so these carry
    // plain values rather than pointers into engine state.
    void targetCreated(qint64 pid);
    void targetStopped();
    void targetExited(int exitCode);
    void engineError(const QString& error);

private slots:
    void onStopped();
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
    void stopEngine();

    DbgEngine* mEngine = nullptr;
    std::thread mStartThread;
    std::atomic<bool> mStartReturned{false};

    RegisterTable* mRegisters = nullptr;
    HexDump* mMemory = nullptr;
    EngineMemoryPage* mMemoryPage = nullptr;
    Architecture* mArchitecture = nullptr;

    bool mDescriptorsSet = false;
};
