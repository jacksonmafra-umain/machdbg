#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The target's threads, one row each.
//
// The "stopped" column comes from the engine's own flag rather than from the kernel's run
// state, and that is not a shortcut: a thread parked in an unanswered exception reply reads as
// "waiting" with a suspend count of zero, and so does every thread of a task suspended by
// Pause(). The kernel describes what each thread was doing when it was frozen, never that it is
// frozen. Both columns are shown, because they answer different questions.
class ThreadTable : public StdTable
{
    Q_OBJECT

public:
    explicit ThreadTable(QWidget* parent = nullptr);

    void setThreads(const std::vector<DbgThread>& threads);

    QStringList rows() const;

private:
    std::vector<DbgThread> mThreads;
};
