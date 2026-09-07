#pragma once
#ifndef QT_NO_ACCESSIBILITY
#include <array>
#include <vector>
#include <QAccessibleWidget>
#include "Gui/RegistersView.h"

class AccessibleRegistersView;

class AccessibleRegistersViewItem : public QAccessibleInterface
{
    RegistersView::REGISTER_NAME id;
    AccessibleRegistersView* mParent;
public:
    AccessibleRegistersViewItem(AccessibleRegistersView* parent, RegistersView::REGISTER_NAME id);

    QString text(QAccessible::Text t) const override;
    QColor foregroundColor() const override;
    QWindow* window() const override;
    QAccessibleInterface* parent() const override;
    QAccessibleInterface* child(int index) const override;
    QAccessibleInterface* childAt(int x, int y) const override;
    QObject* object() const override;
    void setText(QAccessible::Text t, const QString & text) override;
    QRect rect() const override;
    int indexOfChild(const QAccessibleInterface* child) const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    int childCount() const override;
    bool isValid() const override;
};

class AccessibleRegistersView : public QAccessibleWidget
{
    mutable std::array<QAccessible::Id, (size_t)RegistersView::UNKNOWN> interfaces;
    friend class AccessibleRegistersViewItem;
    RegistersView* m_registersView;
public:
    AccessibleRegistersView(QWidget* w);
    ~AccessibleRegistersView();
    int childCount() const override;
    QAccessibleInterface* child(int index) const override;
    QAccessibleInterface* childAt(int x, int y) const override;
    int indexOfChild(const QAccessibleInterface* child) const override;
    QAccessibleInterface* focusChild() const override;
    bool isValid() const override;
    QAccessible::State state() const override;
    QRect rect() const override;
    QAccessibleInterface* interfaceForRegister(RegistersView::REGISTER_NAME reg) const;
private:
    std::vector<RegistersView::REGISTER_NAME> registerOrder() const;
};

#endif
