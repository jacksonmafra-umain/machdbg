#pragma once

#include "RegistersView.h"

class TraceWidget;

class TraceRegisters : public RegistersView
{
    Q_OBJECT
public:
    TraceRegisters(TraceWidget* parent = nullptr);

    void setRegisters(REGDUMP* registers, const REGDUMP* previousRegisters = nullptr);
    void setActive(bool isActive);

public slots:
    virtual void displayCustomContextMenuSlot(QPoint pos);
    virtual void refreshShortcutsSlot();
    void onCopySIMDRegister();
    void onSetCurrentRegister();
    void onFollowInDump();
    void onFollowInStack();
    void onHighlightSlot();

protected:
    virtual void mouseDoubleClickEvent(QMouseEvent* event);

private:
    TraceWidget* mParent;
    QAction* wCM_CopySIMDRegister;
    QAction* wCM_SetCurrentRegister;
    QAction* wCM_FollowInDump;
    QAction* wCM_FollowInStack;
    QAction* wCM_Highlight;
};
