#include "RegisterTable.h"
#include "RegisterFormat.h"

#include <QInputDialog>
#include <QLineEdit>

namespace
{
    constexpr int kNameColumn = 0;
    constexpr int kValueColumn = 1;

    // Userdata on the value cell, so "changed since the last stop" survives in the table itself
    // rather than only in this widget's parallel vector -- getCellUserdata() is what a future
    // paintContent override (or a test) reads it back through.
    constexpr duint kUnchanged = 0;
    constexpr duint kChanged = 1;
}

RegisterTable::RegisterTable(QWidget* parent)
    : StdTable(parent)
{
    // Named, not merely present: task 7's accessibility check finds this table by name rather
    // than by position, so rearranging the app's layout cannot silently turn that check into a
    // no-op.
    setAccessibleName(tr("Registers"));
    setWindowTitle(tr("Registers"));

    addColumnAt(80, tr("Register"), true);
    addColumnAt(200, tr("Value"), true);

    connect(this, &StdTable::doubleClickedSignal, this, &RegisterTable::onDoubleClicked);
}

void RegisterTable::setDescriptors(const DbgRegisterDesc* descs, const uint32_t count)
{
    mDescs.clear();
    if(descs != nullptr)
        mDescs.assign(descs, descs + count);

    mPrevious.assign(mDescs.size(), 0);
    mChanged.assign(mDescs.size(), false);
    // Reset with the table: a snapshot from the previous architecture (or the previous session)
    // would mark rows as changed against values that no longer mean anything.
    mHavePrevious = false;

    setRowCount(mDescs.size());
    for(std::size_t i = 0; i < mDescs.size(); ++i)
    {
        setCellContent(i, kNameColumn, QString::fromUtf8(mDescs[i].name));
        setCellContent(i, kValueColumn, QString(), kUnchanged);
    }
    reloadData();
}

void RegisterTable::setRegisters(const DbgRegisters& regs)
{
    for(std::size_t i = 0; i < mDescs.size(); ++i)
    {
        const uint64_t value = machdbg::views::ValueThroughDescriptor(regs, mDescs[i]);
        const QString text = QString::fromStdString(
            machdbg::views::FormatValue(value, mDescs[i].bits));

        // Only once a first snapshot exists: on the very first stop every register would
        // otherwise read as changed, which says nothing and teaches the eye to ignore the mark.
        const bool changed = mHavePrevious && value != mPrevious[i];
        mChanged[i] = changed;
        mPrevious[i] = value;
        setCellContent(i, kValueColumn, text, changed ? kChanged : kUnchanged);
    }
    mHavePrevious = true;
    reloadData();
}

bool RegisterTable::rowChanged(const uint32_t row) const
{
    return row < mChanged.size() && mChanged[row];
}

void RegisterTable::onDoubleClicked()
{
    const duint row = getInitialSelection();
    if(row >= mDescs.size())
        return;

    bool accepted = false;
    const QString entered = QInputDialog::getText(this, tr("Edit register"),
        tr("New value for %1 (hex)").arg(QString::fromUtf8(mDescs[row].name)),
        QLineEdit::Normal, getCellContent(row, kValueColumn), &accepted);
    if(!accepted)
        return;

    bool parsed = false;
    const qulonglong value = entered.trimmed().toULongLong(&parsed, 16);
    if(!parsed)
    {
        // Refused rather than written as zero: a typo in a register editor is a way to corrupt a
        // debugging session, and the target is the one thing a view must not guess at. The cell
        // keeps the value it is showing, which is still what the target holds.
        return;
    }

    emit registerEdited(QString::fromUtf8(mDescs[row].name), value);
}
