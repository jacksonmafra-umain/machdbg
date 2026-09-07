#pragma once
#ifndef QT_NO_ACCESSIBILITY
#include <QAccessibleWidget>
#include "AccessibleAbstractTableView.h"
#include "Disassembly.h"

class AccessibleDisassembly : public AccessibleAbstractTableView
{
public:
    AccessibleDisassembly(QWidget* w);
    ~AccessibleDisassembly();

    bool isRowSelected(int row) const override;
    QList<int> selectedRows() const override;
    int selectedRowCount() const override;
    int selectedCellCount() const override;
    QList<QAccessibleInterface*> selectedCells() const override;
private:
    QString getCellContent(int row, int col) const override; // Get plain text of a cell
    Disassembly* dis() const;
};

#endif