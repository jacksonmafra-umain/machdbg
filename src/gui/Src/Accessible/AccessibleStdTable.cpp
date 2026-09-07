// This file implements accessibility interface for StdTable
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleStdTable.h"
#include "StdTable.h"

AccessibleStdTable::AccessibleStdTable(QWidget* w) : AccessibleAbstractTableView(w)
{
}

AccessibleStdTable::~AccessibleStdTable()
{
}

bool AccessibleStdTable::isRowSelected(int row) const
{
    auto table = this->table();
    return row >= 0
           && row < rowCount()
           && table->getInitialSelection() == static_cast<duint>(row) + table->getTableOffset();
}

QString AccessibleStdTable::getCellContent(int row, int column) const
{
    // row excludes title
    auto table = this->table();
    return table->getCellContent(table->getTableOffset() + row, column);
}

AbstractStdTable* AccessibleStdTable::table() const
{
    return dynamic_cast<AbstractStdTable*>(m_tableView);
}

// TODO: multi-selection
int AccessibleStdTable::selectedRowCount() const
{
    return selectedRows().size();
}

QList<int> AccessibleStdTable::selectedRows() const
{
    auto table = this->table();
    const duint selection = table->getInitialSelection();
    const duint offset = table->getTableOffset();
    if(selection >= offset && selection - offset < static_cast<duint>(rowCount()))
        return QList<int>({static_cast<int>(selection - offset)});
    return QList<int>();
}

int AccessibleStdTable::selectedCellCount() const
{
    return AccessibleAbstractTableView::selectedCellCount();
}

QList<QAccessibleInterface*> AccessibleStdTable::selectedCells() const
{
    return AccessibleAbstractTableView::selectedCells();
}
#endif