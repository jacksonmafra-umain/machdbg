// This file implements accessibility interface for HexDump
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleHexDump.h"
#include "HexDump.h"
#include "Bridge.h"

AccessibleHexDump::AccessibleHexDump(QWidget* w) : AccessibleAbstractTableView(w)
{
}

AccessibleHexDump::~AccessibleHexDump()
{
}

HexDump* AccessibleHexDump::dump() const
{
    return dynamic_cast<HexDump*>(m_tableView);
}

static int findFirstSelection(HexDump* dump)
{
    const duint bytesPerRow = dump->getBytePerRowCount();
    if(bytesPerRow == 0)
        return -1;

    const duint selection = dump->getInitialSelection();
    const duint firstAddress = dump->getTableOffsetRva();
    const duint visibleSize = dump->getViewableRowsCount() * bytesPerRow;
    if(selection >= firstAddress && selection - firstAddress < visibleSize)
        return static_cast<int>((selection - firstAddress) / bytesPerRow);
    return -1;
}

bool AccessibleHexDump::isRowSelected(int row) const
{
    return row >= 0 && row < rowCount() && row == findFirstSelection(dump());
}

// TODO: multi-selection
int AccessibleHexDump::selectedRowCount() const
{
    return selectedRows().size();
}

QList<int> AccessibleHexDump::selectedRows() const
{
    const int selectedRow = findFirstSelection(dump());
    if(selectedRow >= 0 && selectedRow < rowCount())
        return QList<int>({selectedRow});
    return QList<int>();
}

int AccessibleHexDump::selectedCellCount() const
{
    return AccessibleAbstractTableView::selectedCellCount();
}

QList<QAccessibleInterface*> AccessibleHexDump::selectedCells() const
{
    return AccessibleAbstractTableView::selectedCells();
}

QString AccessibleHexDump::getCellContent(int row, int col) const
{
    const HexDump & dump = *this->dump();
    RichTextPainter::List richText;
    // Compute RVA
    duint rva = row * dump.getBytePerRowCount() + dump.getTableOffsetRva();
    dump.getColumnRichText(col, rva, richText);
    QString str;
    for(const auto & i : richText)
    {
        str += i.text;
    }
    if(col == 0)
        return str.trimmed();
    return str.simplified();
}

#endif
