#include "BreakpointTable.h"

namespace
{
    constexpr int kAddressColumn = 0;
    constexpr int kKindColumn = 1;
    constexpr int kSizeColumn = 2;
    constexpr int kStateColumn = 3;
}

BreakpointTable::BreakpointTable(QWidget* parent)
    : StdTable(parent)
{
    // Named, for the same reason RegisterTable is: the accessibility check finds tables by name
    // rather than by position, so rearranging this window cannot turn that check into a no-op.
    setAccessibleName(tr("Breakpoints"));
    setWindowTitle(tr("Breakpoints"));

    addColumnAt(140, tr("Address"), true);
    addColumnAt(120, tr("Kind"), true);
    addColumnAt(50, tr("Size"), true);
    addColumnAt(110, tr("State"), true);

    connect(this, &StdTable::doubleClickedSignal, this, &BreakpointTable::onDoubleClicked);
}

QString BreakpointTable::kindName(const DbgBreakpointKind kind)
{
    switch(kind)
    {
    case DbgBreakpointKind_Software: return tr("software");
    case DbgBreakpointKind_HwExec: return tr("hardware exec");
    case DbgBreakpointKind_HwRead: return tr("hardware read");
    case DbgBreakpointKind_HwWrite: return tr("hardware write");
    }
    return tr("unknown");
}

void BreakpointTable::setBreakpoints(const std::vector<Row>& rows)
{
    mRows = rows;
    setRowCount(mRows.size());
    for(std::size_t i = 0; i < mRows.size(); ++i)
    {
        const Row& row = mRows[i];
        setCellContent(i, kAddressColumn,
                       QStringLiteral("0x%1").arg(row.address, 0, 16));
        setCellContent(i, kKindColumn, kindName(row.kind));
        setCellContent(i, kSizeColumn, row.size != 0 ? QString::number(row.size) : QString());

        // Three states, not two, because "the user switched it off" and "the engine could not
        // arm it" are different things and only one of them is the user's doing.
        const QString state = row.effective
            ? tr("armed")
            : (row.enabled ? tr("not armed") : tr("disabled"));
        setCellContent(i, kStateColumn, state);
    }
    reloadData();
}

uint64_t BreakpointTable::selectedAddress() const
{
    const duint row = const_cast<BreakpointTable*>(this)->getInitialSelection();
    return row < mRows.size() ? mRows[row].address : 0;
}

QStringList BreakpointTable::rows() const
{
    QStringList out;
    out.reserve(static_cast<int>(mRows.size()));
    auto* self = const_cast<BreakpointTable*>(this);
    for(std::size_t i = 0; i < mRows.size(); ++i)
    {
        // Read back out of the table rather than out of mRows: what a caller wants to know is
        // what the widget is *showing*, which is what a stale or half-filled table gets wrong.
        QString entry = self->getCellContent(i, kAddressColumn) + QLatin1Char(' ') +
                        self->getCellContent(i, kKindColumn);
        const QString size = self->getCellContent(i, kSizeColumn);
        if(!size.isEmpty())
            entry += QLatin1Char(' ') + size;
        out.append(entry + QLatin1Char(' ') + self->getCellContent(i, kStateColumn));
    }
    return out;
}

void BreakpointTable::onDoubleClicked()
{
    const uint64_t address = selectedAddress();
    if(address != 0)
        emit enabledToggleRequested(address);
}
