#pragma once

#include "RegistersView.h"
#include "CommonActions.h"

class CPUWidget;
class CPUMultiDump;

typedef struct
{
    const char* string;
    unsigned int value;
} STRING_VALUE_TABLE_t;

#define SIZE_TABLE(table) (sizeof(table) / sizeof(*table))

class CPURegistersView : public RegistersView
{
    Q_OBJECT
public:
    CPURegistersView(CPUWidget* parent = nullptr);
    void setRegisterData(REGISTER_NAME reg, const void* data, size_t size);

public slots:
    void setRegister(REGISTER_NAME reg, duint value);
    void updateRegistersSlot();
    virtual void debugStateChangedSlot(DBGSTATE state);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseDoubleClickEvent(QMouseEvent* event);
    virtual void keyPressEvent(QKeyEvent* event);
    virtual void refreshShortcutsSlot();
    virtual void displayCustomContextMenuSlot(QPoint pos);

protected slots:
    void onIncrementx87StackAction();
    void onDecrementx87StackAction();
    void onModifyAction();
    void onIncrementAction();
    void onDecrementAction();
    void onZeroAction();
    void onToggleValueAction();
    void onUndoAction();
    void onCopyPreviousAction();
    void onFollowInDisassembly();
    void onFollowInDump();
    void onFollowInDumpN();
    void onFollowInStack();
    void onFollowInMemoryMap();
    void onRemoveHardware();
    void onHighlightSlot();
    void ModifyFields(const QString & title, STRING_VALUE_TABLE_t* table, SIZE_T size);
    void disasmSelectionChangedSlot(duint va);

private:
    QString registerNameForSet(REGISTER_NAME reg) const;
    void CreateDumpNMenu(QMenu* dumpMenu);
    void displayEditDialog();
    void setupContextMenu();

    MenuBuilder* mMenuBuilder;
    CommonActions* mCommonActions;
    CPUWidget* mParent;
    // context menu actions
    QAction* mFollowInDump;
    QAction* wCM_Modify;
    QAction* wCM_Increment;
    QAction* wCM_Decrement;
    QAction* wCM_Zero;
    QAction* wCM_ToggleValue;
    QAction* wCM_CopyPrevious;
    QAction* wCM_Undo;
    QAction* wCM_FollowInDisassembly;
    QAction* wCM_FollowInDump;
    QAction* wCM_FollowInStack;
    QAction* wCM_FollowInMemoryMap;
    QAction* wCM_RemoveHardware;
    QAction* wCM_Incrementx87Stack;
    QAction* wCM_Decrementx87Stack;
    QAction* wCM_Highlight;
};
