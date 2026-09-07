#pragma once
#ifndef QT_NO_ACCESSIBILITY
#include <QAccessibleWidget>
#include "../BasicView/AbstractTableView.h"

/**
* Accessible interface for AbstractTableView.
* This accessibility interface only exposes what is visible on the screen because the data scale of underlying AbstractTableView is uncontrollable.
* Its direct-child layout matches QAccessibleTable with horizontal and vertical
* headers: a hidden corner, column headers, then a hidden row header followed by
* each row's data cells. Row-header names use the first raw data column, like a
* QTableView model whose vertical headerData contains debugger addresses.
* Like Qt's item-view adapter, virtual children retain the widget and model
* identity and re-query this adapter instead of retaining its address.
*/
class AccessibleAbstractTableView : public QAccessibleWidget, public QAccessibleTableInterface
{
    std::vector<QAccessible::Id> cellInterfaces;
    std::vector<QAccessible::Id> columnTitleInterfaces;
    std::vector<QAccessible::Id> rowTitleInterfaces;
    QAccessible::Id cornerInterface = 0;
    int rows = 0;
    int cols = 0;
    duint tableOffset = 0;
    quint64 modelRevision = 0;
    bool m_destroying = false;
    friend class AccessibleAbstractTableViewCell;
    friend class AccessibleAbstractTableViewCellTitle;
    std::vector<duint> m_visibleColumns;
public:
    AccessibleAbstractTableView(QWidget* w);
    ~AccessibleAbstractTableView();

    AbstractTableView* getTable() const;
    // Convert from displayed column to raw column (not reordered)
    duint logicalColumn(int physicalColumn) const;
    // QAccessibleInterface
    int childCount() const override;
    QAccessibleInterface* child(int index) const override;
    QAccessibleInterface* childAt(int x, int y) const override;
    int indexOfChild(const QAccessibleInterface* child) const override;
    QAccessibleInterface* focusChild() const override;
    bool isValid() const override;
    QAccessible::State state() const override;
    void* interface_cast(QAccessible::InterfaceType type) override;
    // QAccessibleTableInterface. Rows and columns are zero-based data coordinates;
    // structural headers are separate children and are not part of rowCount().
    QAccessibleInterface* caption() const override;
    QAccessibleInterface* cellAt(int row, int column) const override;
    int columnCount() const override;
    QString columnDescription(int column) const override;
    bool isColumnSelected(int column) const override;
    bool isRowSelected(int row) const override;
    void modelChange(QAccessibleTableModelChangeEvent* event) override;
    int rowCount() const override;
    QString rowDescription(int row) const override;
    bool selectColumn(int column) override;
    bool selectRow(int row) override;
    int selectedCellCount() const override;
    QList<QAccessibleInterface*> selectedCells() const override;
    int selectedColumnCount() const override;
    QList<int> selectedColumns() const override;
    int selectedRowCount() const override;
    QList<int> selectedRows() const override;
    QAccessibleInterface* summary() const override;
    bool unselectColumn(int column) override;
    bool unselectRow(int row) override;
protected:
    virtual QString getCellContent(int row, int col) const; // Get plain text of a cell
    AbstractTableView* m_tableView;
private:
    std::vector<QAccessible::Id> takeChildInterfaces();
    static void deleteChildInterfaces(const std::vector<QAccessible::Id>& ids);
    void clearChildInterfaces();
    std::vector<duint> visibleColumns() const;
    int visibleRowCount() const;
    bool modelIsCurrent() const;
    void ensureModelUpToDate() const;
    QAccessibleInterface* cellInterface(int row, int column) const;
    QAccessibleInterface* columnHeaderInterface(int column) const;
    QAccessibleInterface* rowHeaderInterface(int row) const;
    QAccessibleInterface* cornerHeaderInterface() const;
    QRect elementRect(int row, int column, bool header) const;
    bool cellIsValid(int row, int column) const;
    bool headerIsValid(int column) const;
    bool rowHeaderIsValid(int row) const;
    bool cornerIsValid() const;
};

#endif
