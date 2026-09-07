// This file implements accessibility interface for AbstractTableView
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleAbstractTableView.h"
#include "AccessibleAbstractTableViewCell.h"
#include "AccessibleAbstractTableViewCellTitle.h"
#include <algorithm>
#include <exception>

AccessibleAbstractTableView::AccessibleAbstractTableView(QWidget* w)
    : QAccessibleWidget(w, QAccessible::Table, dynamic_cast<AbstractTableView*>(w)->accessibleName())
    , m_tableView(dynamic_cast<AbstractTableView*>(w))
{
    assert(m_tableView);
    m_tableView->prepareData();
    modelChange(nullptr);
}

AccessibleAbstractTableView::~AccessibleAbstractTableView()
{
    // Publish an empty, invalid tree before deleting virtual children. Qt
    // 6.11+ can synchronously query this adapter from ObjectDestroyed handling.
    m_destroying = true;
    rows = 0;
    cols = 0;
    tableOffset = 0;
    m_visibleColumns.clear();
    clearChildInterfaces();
}

QString AccessibleAbstractTableView::getCellContent(int row, int column) const
{
    return QString("Row %1 Column %2").arg(row).arg(column);
}

AbstractTableView* AccessibleAbstractTableView::getTable() const
{
    return m_tableView;
}

std::vector<QAccessible::Id> AccessibleAbstractTableView::takeChildInterfaces()
{
    std::vector<QAccessible::Id> result;
    result.reserve(1
                   + columnTitleInterfaces.size()
                   + rowTitleInterfaces.size()
                   + cellInterfaces.size());
    result.push_back(cornerInterface);
    result.insert(result.end(), columnTitleInterfaces.begin(), columnTitleInterfaces.end());
    result.insert(result.end(), rowTitleInterfaces.begin(), rowTitleInterfaces.end());
    result.insert(result.end(), cellInterfaces.begin(), cellInterfaces.end());

    // Detach every old ID before deletion. Qt 6.11+ emits synchronous
    // ObjectDestroyed notifications for objectless interfaces, so callbacks
    // must observe an empty/current cache rather than IDs being torn down.
    cornerInterface = 0;
    columnTitleInterfaces.clear();
    rowTitleInterfaces.clear();
    cellInterfaces.clear();
    return result;
}

void AccessibleAbstractTableView::deleteChildInterfaces(const std::vector<QAccessible::Id>& ids)
{
    for(const auto id : ids)
    {
        if(id != 0)
            QAccessible::deleteAccessibleInterface(id);
    }
}

void AccessibleAbstractTableView::clearChildInterfaces()
{
    deleteChildInterfaces(takeChildInterfaces());
}

std::vector<duint> AccessibleAbstractTableView::visibleColumns() const
{
    std::vector<duint> result;
    if(!m_tableView)
        return result;

    const int rawColumnCount = static_cast<int>(std::min<duint>(m_tableView->getColumnCount(), 1000));
    result.reserve(rawColumnCount);
    for(int displayColumn = 0; displayColumn < rawColumnCount && displayColumn < m_tableView->mColumnOrder.size(); displayColumn++)
    {
        const duint logical = m_tableView->mColumnOrder[displayColumn];
        if(logical < m_tableView->getColumnCount() && !m_tableView->getColumnHidden(logical))
            result.push_back(logical);
    }
    return result;
}

int AccessibleAbstractTableView::visibleRowCount() const
{
    if(!m_tableView)
        return 0;

    const duint totalRows = m_tableView->getRowCount();
    const duint offset = m_tableView->getTableOffset();
    const duint remainingRows = offset < totalRows ? totalRows - offset : 0;
    return static_cast<int>(std::min<duint>({m_tableView->getViewableRowsCount(), remainingRows, 10000}));
}

bool AccessibleAbstractTableView::modelIsCurrent() const
{
    return !m_destroying
           && m_tableView
           && modelRevision == m_tableView->accessibilityModelRevision
           && tableOffset == m_tableView->getTableOffset()
           && rows == visibleRowCount()
           && m_visibleColumns == visibleColumns();
}

void AccessibleAbstractTableView::ensureModelUpToDate() const
{
    if(m_destroying || !m_tableView)
        return;
    if(!modelIsCurrent())
    {
        // AbstractTableView normally prepares its visible data lazily from
        // paintEvent(). An AT client may query before that paint occurs.
        m_tableView->prepareData();
        const_cast<AccessibleAbstractTableView*>(this)->modelChange(nullptr);
    }
}

duint AccessibleAbstractTableView::logicalColumn(int physicalColumn) const
{
    return m_visibleColumns.at(physicalColumn);
}

QAccessibleInterface* AccessibleAbstractTableView::cellInterface(int row, int column) const
{
    if(row < 0 || column < 0 || row >= rows || column >= cols)
        return nullptr;

    const size_t index = static_cast<size_t>(row) * cols + column;
    auto & id = const_cast<std::vector<QAccessible::Id>&>(cellInterfaces).at(index);
    if(id == 0)
    {
        id = QAccessible::registerAccessibleInterface(
                 new AccessibleAbstractTableViewCell(m_tableView, row, column, modelRevision));
    }
    return QAccessible::accessibleInterface(id);
}

QAccessibleInterface* AccessibleAbstractTableView::columnHeaderInterface(int column) const
{
    if(column < 0 || column >= cols)
        return nullptr;

    auto & id = const_cast<std::vector<QAccessible::Id>&>(columnTitleInterfaces).at(column);
    if(id == 0)
    {
        id = QAccessible::registerAccessibleInterface(
                 new AccessibleAbstractTableViewCellTitle(
                     m_tableView,
                     AccessibleAbstractTableViewCellTitle::HeaderType::Column,
                     column,
                     modelRevision));
    }
    return QAccessible::accessibleInterface(id);
}

QAccessibleInterface* AccessibleAbstractTableView::rowHeaderInterface(int row) const
{
    if(row < 0 || row >= rows)
        return nullptr;

    auto & id = const_cast<std::vector<QAccessible::Id>&>(rowTitleInterfaces).at(row);
    if(id == 0)
    {
        id = QAccessible::registerAccessibleInterface(
                 new AccessibleAbstractTableViewCellTitle(
                     m_tableView,
                     AccessibleAbstractTableViewCellTitle::HeaderType::Row,
                     row,
                     modelRevision));
    }
    return QAccessible::accessibleInterface(id);
}

QAccessibleInterface* AccessibleAbstractTableView::cornerHeaderInterface() const
{
    if(m_destroying || !m_tableView)
        return nullptr;
    auto & id = const_cast<QAccessible::Id &>(cornerInterface);
    if(id == 0)
    {
        id = QAccessible::registerAccessibleInterface(
                 new AccessibleAbstractTableViewCellTitle(
                     m_tableView,
                     AccessibleAbstractTableViewCellTitle::HeaderType::Corner,
                     -1,
                     modelRevision));
    }
    return QAccessible::accessibleInterface(id);
}

int AccessibleAbstractTableView::childCount() const
{
    ensureModelUpToDate();
    return m_destroying ? 0 : (rows + 1) * (cols + 1);
}

QAccessibleInterface* AccessibleAbstractTableView::child(int index) const
{
    ensureModelUpToDate();
    if(m_destroying || index < 0 || index >= (rows + 1) * (cols + 1))
        return nullptr;

    const int stride = cols + 1;
    const int structuralRow = index / stride;
    const int structuralColumn = index % stride;
    if(structuralRow == 0)
    {
        if(structuralColumn == 0)
            return cornerHeaderInterface();
        return columnHeaderInterface(structuralColumn - 1);
    }
    if(structuralColumn == 0)
        return rowHeaderInterface(structuralRow - 1);
    return cellInterface(structuralRow - 1, structuralColumn - 1);
}

QAccessibleInterface* AccessibleAbstractTableView::childAt(int x, int y) const
{
    ensureModelUpToDate();
    const QWidget* viewport = m_tableView ? m_tableView->viewport() : nullptr;
    if(!viewport)
        return nullptr;

    const QPoint globalPosition(x, y);
    const QPoint localPosition = viewport->mapFromGlobal(globalPosition);
    if(!viewport->rect().contains(localPosition))
        return nullptr;

    int column = -1;
    const bool useHeaderGeometry = m_tableView->getHeaderHeight() > 0;
    for(int currentColumn = 0; currentColumn < cols; currentColumn++)
    {
        const QRect candidate = useHeaderGeometry
                                ? elementRect(-1, currentColumn, true)
                                : elementRect(0, currentColumn, false);
        if(!candidate.isEmpty() && candidate.left() <= x && x <= candidate.right())
        {
            column = currentColumn;
            break;
        }
    }
    if(column < 0)
        return nullptr;

    QAccessibleInterface* result = nullptr;
    if(localPosition.y() < m_tableView->getHeaderHeight())
    {
        result = columnHeaderInterface(column);
    }
    else
    {
        const int row = m_tableView->getIndexOffsetFromY(localPosition.y() - m_tableView->getHeaderHeight());
        result = cellInterface(row, column);
    }

    return result && result->rect().contains(globalPosition) ? result : nullptr;
}

int AccessibleAbstractTableView::indexOfChild(const QAccessibleInterface* child) const
{
    if(!child)
        return -1;
    for(int i = 0; i < childCount(); i++)
    {
        if(this->child(i) == child)
            return i;
    }
    return -1;
}

QAccessibleInterface* AccessibleAbstractTableView::focusChild() const
{
    ensureModelUpToDate();
    if(!m_tableView->hasFocus())
        return nullptr;

    const int row = m_tableView->accessibilitySelectedRow();
    const int column = m_tableView->accessibilitySelectedColumn;
    if(auto cell = cellInterface(row, column))
        return cell;
    return const_cast<AccessibleAbstractTableView*>(this);
}

bool AccessibleAbstractTableView::isValid() const
{
    return !m_destroying && QAccessibleWidget::isValid() && m_tableView;
}

QAccessible::State AccessibleAbstractTableView::state() const
{
    QAccessible::State result = QAccessibleWidget::state();
    result.readOnly = true;
    result.multiLine = true;
    result.multiSelectable = false;
    return result;
}

void* AccessibleAbstractTableView::interface_cast(QAccessible::InterfaceType type)
{
    if(type == QAccessible::TableInterface)
        return static_cast<QAccessibleTableInterface*>(this);
    return QAccessibleWidget::interface_cast(type);
}

QAccessibleInterface* AccessibleAbstractTableView::caption() const
{
    return nullptr;
}

QAccessibleInterface* AccessibleAbstractTableView::cellAt(int row, int column) const
{
    ensureModelUpToDate();
    // Match QAccessibleTable's header extension to the public data-cell API.
    if(row == -1)
        return column == -1 ? cornerHeaderInterface() : columnHeaderInterface(column);
    if(column == -1)
        return rowHeaderInterface(row);
    return cellInterface(row, column);
}

int AccessibleAbstractTableView::columnCount() const
{
    ensureModelUpToDate();
    return cols;
}

QString AccessibleAbstractTableView::columnDescription(int column) const
{
    ensureModelUpToDate();
    if(column < 0 || column >= cols)
        return QString();
    return m_tableView->getColTitle(logicalColumn(column));
}

bool AccessibleAbstractTableView::isColumnSelected(int column) const
{
    Q_UNUSED(column);
    // The custom views select rows, not entire columns. accessibilitySelectedColumn
    // is the current/focused cell within that row.
    return false;
}

bool AccessibleAbstractTableView::isRowSelected(int row) const
{
    ensureModelUpToDate();
    return row >= 0 && row < rows && m_tableView->accessibilitySelectedRow() == row;
}

void AccessibleAbstractTableView::modelChange(QAccessibleTableModelChangeEvent* event)
{
    Q_UNUSED(event);
    if(m_destroying)
        return;

    // Every event emitted by AbstractTableView is a model reset. Visible
    // coordinates have new identity after scrolling even if dimensions match.
    // Publish the new generation and empty cache before deleting old IDs: Qt
    // 6.11+ synchronously dispatches ObjectDestroyed for virtual interfaces,
    // and Cocoa may re-query this adapter from that callback.
    const auto oldInterfaces = takeChildInterfaces();
    modelRevision = m_tableView ? m_tableView->accessibilityModelRevision : 0;
    tableOffset = m_tableView ? m_tableView->getTableOffset() : 0;
    rows = visibleRowCount();
    m_visibleColumns = visibleColumns();
    cols = static_cast<int>(m_visibleColumns.size());
    cellInterfaces.assign(static_cast<size_t>(rows) * cols, 0);
    columnTitleInterfaces.assign(cols, 0);
    rowTitleInterfaces.assign(rows, 0);
    cornerInterface = 0;
    deleteChildInterfaces(oldInterfaces);
}

int AccessibleAbstractTableView::rowCount() const
{
    ensureModelUpToDate();
    return rows;
}

QString AccessibleAbstractTableView::rowDescription(int row) const
{
    ensureModelUpToDate();
    if(row < 0 || row >= rows || !m_tableView || m_tableView->getColumnCount() == 0)
        return QString();
    try
    {
        return getCellContent(row, 0);
    }
    catch(const std::exception &)
    {
        return QString();
    }
}

bool AccessibleAbstractTableView::selectColumn(int column)
{
    Q_UNUSED(column);
    return false;
}

bool AccessibleAbstractTableView::selectRow(int row)
{
    Q_UNUSED(row);
    return false;
}

int AccessibleAbstractTableView::selectedCellCount() const
{
    return selectedRows().size() * columnCount();
}

QList<QAccessibleInterface*> AccessibleAbstractTableView::selectedCells() const
{
    QList<QAccessibleInterface*> result;
    const auto selected = selectedRows();
    result.reserve(selected.size() * columnCount());
    for(const int row : selected)
    {
        for(int column = 0; column < columnCount(); column++)
        {
            if(auto cell = cellAt(row, column))
                result.append(cell);
        }
    }
    return result;
}

int AccessibleAbstractTableView::selectedColumnCount() const
{
    return 0;
}

QList<int> AccessibleAbstractTableView::selectedColumns() const
{
    return QList<int>();
}

int AccessibleAbstractTableView::selectedRowCount() const
{
    return selectedRows().size();
}

QList<int> AccessibleAbstractTableView::selectedRows() const
{
    ensureModelUpToDate();
    const int row = m_tableView->accessibilitySelectedRow();
    if(row >= 0 && row < rows)
        return QList<int>({row});
    return QList<int>();
}

QAccessibleInterface* AccessibleAbstractTableView::summary() const
{
    return nullptr;
}

bool AccessibleAbstractTableView::unselectColumn(int column)
{
    Q_UNUSED(column);
    return false;
}

bool AccessibleAbstractTableView::unselectRow(int row)
{
    Q_UNUSED(row);
    return false;
}

bool AccessibleAbstractTableView::cellIsValid(int row, int column) const
{
    return isValid() && modelIsCurrent() && row >= 0 && row < rows && column >= 0 && column < cols;
}

bool AccessibleAbstractTableView::headerIsValid(int column) const
{
    return isValid() && modelIsCurrent() && column >= 0 && column < cols;
}

bool AccessibleAbstractTableView::rowHeaderIsValid(int row) const
{
    return isValid() && modelIsCurrent() && row >= 0 && row < rows;
}

bool AccessibleAbstractTableView::cornerIsValid() const
{
    return isValid() && modelIsCurrent();
}

QRect AccessibleAbstractTableView::elementRect(int row, int column, bool header) const
{
    if((header && !headerIsValid(column)) || (!header && !cellIsValid(row, column)))
        return QRect();

    QWidget* viewport = m_tableView->viewport();
    if(!viewport || !viewport->isVisible())
        return QRect();

    int left = -m_tableView->horizontalScrollBar()->value();
    for(int currentColumn = 0; currentColumn < column; currentColumn++)
        left += m_tableView->getColumnWidth(logicalColumn(currentColumn));

    const int top = header ? 0 : m_tableView->getHeaderHeight() + row * m_tableView->getRowHeight();
    const int height = header ? m_tableView->getHeaderHeight() : m_tableView->getRowHeight();
    QRect localRect(left, top, m_tableView->getColumnWidth(logicalColumn(column)), height);
    localRect = localRect.intersected(viewport->rect());
    if(localRect.isEmpty())
        return QRect();
    return QRect(viewport->mapToGlobal(localRect.topLeft()), localRect.size());
}

#endif
