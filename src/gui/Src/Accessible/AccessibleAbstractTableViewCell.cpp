// This file implements accessibility interface for table cells of AbstractTableView
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleAbstractTableViewCell.h"
#include "AccessibleAbstractTableView.h"
#include <exception>

AccessibleAbstractTableViewCell::AccessibleAbstractTableViewCell(AbstractTableView* tableView, int row, int column, quint64 modelRevision)
    : row(row)
    , column(column)
    , mModelRevision(modelRevision)
    , mTableView(tableView)
{
}

AccessibleAbstractTableView* AccessibleAbstractTableViewCell::accessibleTable() const
{
    // Match QAccessibleTableCell: virtual children retain the QObject-backed
    // view and resolve its current adapter instead of retaining an adapter that
    // may be synchronously replaced by a platform accessibility bridge.
    return dynamic_cast<AccessibleAbstractTableView*>(parent());
}

bool AccessibleAbstractTableViewCell::belongsTo(const AccessibleAbstractTableView* table) const
{
    // Visible coordinates are our equivalent of QTableView's persistent model
    // index. A cell from before a viewport/model reset must not become valid
    // merely because a replacement adapter has the same dimensions.
    return table
           && mTableView
           && table->getTable() == mTableView.data()
           && table->modelRevision == mModelRevision;
}

QString AccessibleAbstractTableViewCell::text(QAccessible::Text t) const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    if(!belongsTo(accessible) || !accessible->cellIsValid(row, column))
        return QString();
    switch(t)
    {
    case QAccessible::Name:
        try
        {
            return accessible->getCellContent(row, accessible->logicalColumn(column));
        }
        catch(const std::exception &)
        {
            // Accessibility APIs must reject stale virtual cells, not propagate
            // an out-of-range exception into a native platform bridge.
            return QString();
        }
    default:
        return QString();
    }
}

QColor AccessibleAbstractTableViewCell::foregroundColor() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return belongsTo(accessible) && accessible->cellIsValid(row, column) && mTableView
           ? mTableView->mTextColor
           : QColor();
}

int AccessibleAbstractTableViewCell::childCount() const
{
    return 0;
}

QWindow* AccessibleAbstractTableViewCell::window() const
{
    if(AccessibleAbstractTableView* accessible = accessibleTable())
        return accessible->window();
    return nullptr;
}

QAccessibleInterface* AccessibleAbstractTableViewCell::parent() const
{
    return mTableView ? QAccessible::queryAccessibleInterface(mTableView.data()) : nullptr;
}

QAccessibleInterface* AccessibleAbstractTableViewCell::child(int index) const
{
    Q_UNUSED(index);
    return nullptr;
}

int AccessibleAbstractTableViewCell::indexOfChild(const QAccessibleInterface* child) const
{
    Q_UNUSED(child);
    return -1;
}

QAccessible::Role AccessibleAbstractTableViewCell::role() const
{
    return QAccessible::Cell;
}

QAccessible::State AccessibleAbstractTableViewCell::state() const
{
    QAccessible::State result;
    AccessibleAbstractTableView* accessible = accessibleTable();
    const AbstractTableView* table = mTableView.data();
    if(!belongsTo(accessible) || !accessible->cellIsValid(row, column) || !table)
    {
        result.invalid = true;
        return result;
    }

    const bool enabled = table->isEnabled();
    const bool visible = !rect().isEmpty();
    result.disabled = !enabled;
    result.focusable = enabled;
    result.selectable = enabled;
    result.selected = accessible->isRowSelected(row);
    result.focused = result.selected
                     && table->accessibilitySelectedColumn == column
                     && table->hasFocus();
    result.readOnly = true;
    result.invisible = !table->isVisible() || !visible;
    result.offscreen = !visible;
    return result;
}

QAccessibleInterface* AccessibleAbstractTableViewCell::childAt(int x, int y) const
{
    Q_UNUSED(x);
    Q_UNUSED(y);
    return nullptr;
}

QObject* AccessibleAbstractTableViewCell::object() const
{
    return nullptr;
}

void AccessibleAbstractTableViewCell::setText(QAccessible::Text t, const QString & text)
{
    Q_UNUSED(t);
    Q_UNUSED(text);
}

QRect AccessibleAbstractTableViewCell::rect() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return belongsTo(accessible) ? accessible->elementRect(row, column, false) : QRect();
}

bool AccessibleAbstractTableViewCell::isValid() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return belongsTo(accessible) && accessible->cellIsValid(row, column);
}

void* AccessibleAbstractTableViewCell::interface_cast(QAccessible::InterfaceType type)
{
    if(type == QAccessible::TableCellInterface)
        return static_cast<QAccessibleTableCellInterface*>(this);
    return nullptr;
}

bool AccessibleAbstractTableViewCell::isSelected() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return belongsTo(accessible) && accessible->cellIsValid(row, column) && accessible->isRowSelected(row);
}

QList<QAccessibleInterface*> AccessibleAbstractTableViewCell::columnHeaderCells() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    if(!belongsTo(accessible) || !accessible->cellIsValid(row, column))
        return QList<QAccessibleInterface*>();
    if(auto header = accessible->columnHeaderInterface(column))
        return QList<QAccessibleInterface*>({header});
    return QList<QAccessibleInterface*>();
}

QList<QAccessibleInterface*> AccessibleAbstractTableViewCell::rowHeaderCells() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    if(!belongsTo(accessible) || !accessible->cellIsValid(row, column))
        return QList<QAccessibleInterface*>();
    if(auto header = accessible->rowHeaderInterface(row))
        return QList<QAccessibleInterface*>({header});
    return QList<QAccessibleInterface*>();
}

int AccessibleAbstractTableViewCell::columnIndex() const
{
    return column;
}

int AccessibleAbstractTableViewCell::rowIndex() const
{
    return row;
}

int AccessibleAbstractTableViewCell::columnExtent() const
{
    return 1;
}

int AccessibleAbstractTableViewCell::rowExtent() const
{
    return 1;
}

QAccessibleInterface* AccessibleAbstractTableViewCell::table() const
{
    return parent();
}

#endif
