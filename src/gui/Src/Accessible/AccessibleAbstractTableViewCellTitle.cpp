// This file implements the structural headers of AbstractTableView.
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleAbstractTableViewCellTitle.h"
#include "AccessibleAbstractTableView.h"
#include <exception>

AccessibleAbstractTableViewCellTitle::AccessibleAbstractTableViewCellTitle(
    AbstractTableView* tableView, HeaderType type, int index, quint64 modelRevision)
    : AccessibleAbstractTableViewCell(
          tableView,
          type == HeaderType::Row ? index : -1,
          type == HeaderType::Column ? index : -1,
          modelRevision)
    , mType(type)
{
}

QString AccessibleAbstractTableViewCellTitle::text(QAccessible::Text t) const
{
    if(t != QAccessible::Name)
        return QString();

    AccessibleAbstractTableView* accessible = accessibleTable();
    if(!isValidFor(accessible))
        return QString();

    if(mType == HeaderType::Column)
        return mTableView->getColTitle(accessible->logicalColumn(column));
    if(mType == HeaderType::Row && mTableView->getColumnCount() > 0)
    {
        try
        {
            // Match a QTableView with custom vertical headerData: the dedicated
            // row-header object carries the debugger row's stable identifier,
            // while the first data cell remains an ordinary table cell.
            return accessible->getCellContent(row, 0);
        }
        catch(const std::exception &)
        {
            return QString();
        }
    }
    return QString();
}

QColor AccessibleAbstractTableViewCellTitle::foregroundColor() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return isValidFor(accessible) && mTableView
           ? mTableView->mHeaderTextColor
           : QColor();
}

QColor AccessibleAbstractTableViewCellTitle::backgroundColor() const
{
    AccessibleAbstractTableView* accessible = accessibleTable();
    return isValidFor(accessible) && mTableView
           ? mTableView->mHeaderBackgroundColor
           : QColor();
}

QAccessible::State AccessibleAbstractTableViewCellTitle::state() const
{
    QAccessible::State result;
    AccessibleAbstractTableView* accessible = accessibleTable();
    const AbstractTableView* table = mTableView.data();
    if(!isValidFor(accessible) || !table)
    {
        result.invalid = true;
        return result;
    }

    const bool visible = !rect().isEmpty();
    result.disabled = !table->isEnabled();
    result.readOnly = true;
    // AbstractTableView paints its horizontal header, but has no visible
    // vertical-header gutter or corner. Keep those canonical structural
    // objects available to table APIs while native trees mark them hidden.
    result.invisible = mType != HeaderType::Column
                       || table->getHeaderHeight() == 0
                       || !table->isVisible()
                       || !visible;
    result.offscreen = !visible;
    return result;
}

QRect AccessibleAbstractTableViewCellTitle::rect() const
{
    if(mType != HeaderType::Column)
        return QRect();
    AccessibleAbstractTableView* accessible = accessibleTable();
    return isValidFor(accessible) ? accessible->elementRect(-1, column, true) : QRect();
}

QAccessible::Role AccessibleAbstractTableViewCellTitle::role() const
{
    switch(mType)
    {
    case HeaderType::Column:
        return QAccessible::ColumnHeader;
    case HeaderType::Row:
        return QAccessible::RowHeader;
    case HeaderType::Corner:
        return QAccessible::Pane;
    }
    return QAccessible::NoRole;
}

int AccessibleAbstractTableViewCellTitle::columnIndex() const
{
    return mType == HeaderType::Column ? column : -1;
}

int AccessibleAbstractTableViewCellTitle::rowIndex() const
{
    return mType == HeaderType::Row ? row : -1;
}

bool AccessibleAbstractTableViewCellTitle::isValidFor(AccessibleAbstractTableView* accessible) const
{
    if(!belongsTo(accessible))
        return false;
    switch(mType)
    {
    case HeaderType::Column:
        return accessible->headerIsValid(column);
    case HeaderType::Row:
        return accessible->rowHeaderIsValid(row);
    case HeaderType::Corner:
        return accessible->cornerIsValid();
    }
    return false;
}

bool AccessibleAbstractTableViewCellTitle::isValid() const
{
    return isValidFor(accessibleTable());
}

void* AccessibleAbstractTableViewCellTitle::interface_cast(QAccessible::InterfaceType type)
{
    Q_UNUSED(type);
    // Headers are structural children, not cells in the data grid.
    return nullptr;
}

QList<QAccessibleInterface*> AccessibleAbstractTableViewCellTitle::rowHeaderCells() const
{
    return mType == HeaderType::Row
           ? QList<QAccessibleInterface*>({const_cast<AccessibleAbstractTableViewCellTitle*>(this)})
           : QList<QAccessibleInterface*>();
}

QList<QAccessibleInterface*> AccessibleAbstractTableViewCellTitle::columnHeaderCells() const
{
    return mType == HeaderType::Column
           ? QList<QAccessibleInterface*>({const_cast<AccessibleAbstractTableViewCellTitle*>(this)})
           : QList<QAccessibleInterface*>();
}

#endif
