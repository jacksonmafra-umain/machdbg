#pragma once
#ifndef QT_NO_ACCESSIBILITY
#include <QAccessibleWidget>
#include "AccessibleAbstractTableViewCell.h"

class AccessibleAbstractTableViewCellTitle : public AccessibleAbstractTableViewCell
{
public:
    enum class HeaderType
    {
        Column,
        Row,
        Corner
    };

    AccessibleAbstractTableViewCellTitle(AbstractTableView* tableView, HeaderType type, int index, quint64 modelRevision);
    // QAccessibleInterface
    QString text(QAccessible::Text t) const override;
    QColor foregroundColor() const override;
    QColor backgroundColor() const override;
    QAccessible::State state() const override;
    QList<QAccessibleInterface*> rowHeaderCells() const override;
    QList<QAccessibleInterface*> columnHeaderCells() const override;
    QRect rect() const override;
    QAccessible::Role role() const override;
    int columnIndex() const override;
    int rowIndex() const override;
    bool isValid() const override;
    void* interface_cast(QAccessible::InterfaceType type) override;

private:
    bool isValidFor(AccessibleAbstractTableView* accessible) const;
    HeaderType mType;
};

#endif
