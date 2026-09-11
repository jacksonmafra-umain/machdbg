#include "ThreadTable.h"

namespace
{
    constexpr int kIdColumn = 0;
    constexpr int kNameColumn = 1;
    constexpr int kStateColumn = 2;
    constexpr int kPcColumn = 3;
    constexpr int kStoppedColumn = 4;
}

ThreadTable::ThreadTable(QWidget* parent)
    : StdTable(parent)
{
    setAccessibleName(tr("Threads"));
    setWindowTitle(tr("Threads"));

    addColumnAt(90, tr("Id"), true);
    addColumnAt(140, tr("Name"), true);
    addColumnAt(110, tr("State"), true);
    addColumnAt(140, tr("PC"), true);
    addColumnAt(80, tr("Stopped"), true);
}

void ThreadTable::setThreads(const std::vector<DbgThread>& threads)
{
    mThreads = threads;
    setRowCount(mThreads.size());
    for(std::size_t i = 0; i < mThreads.size(); ++i)
    {
        const DbgThread& thread = mThreads[i];
        setCellContent(i, kIdColumn, QString::number(thread.id));
        setCellContent(i, kNameColumn, QString::fromUtf8(thread.name));
        setCellContent(i, kStateColumn,
                       QString::fromUtf8(thread.runStateName ? thread.runStateName : ""));
        setCellContent(i, kPcColumn, QStringLiteral("0x%1").arg(thread.pc, 0, 16));
        // The engine's answer, not the kernel's -- see the header.
        setCellContent(i, kStoppedColumn,
                       thread.isStoppedThread ? tr("yes") : QString());
    }
    reloadData();
}

QStringList ThreadTable::rows() const
{
    QStringList out;
    out.reserve(static_cast<int>(mThreads.size()));
    auto* self = const_cast<ThreadTable*>(this);
    for(std::size_t i = 0; i < mThreads.size(); ++i)
    {
        out.append(self->getCellContent(i, kIdColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kNameColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kStateColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kPcColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kStoppedColumn));
    }
    return out;
}
