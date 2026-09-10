#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The breakpoint list, as data. Like RegisterTable (see its header for why these views are built
// on StdTable rather than on the vendored x86 ones), this widget knows nothing about the engine:
// it is handed rows and emits what the user asked for. That is what lets it be filled from a
// test, a sample app or the real debugger without changing.
//
// The vtable has no way to enumerate breakpoints, and deliberately so -- the caller is the one
// that set them, so the caller is the one that knows. What it can ask the engine per address is
// whether the target is *carrying* the breakpoint right now (IsBreakpointEffective), and that is
// the "state" column: a row the user disabled and a row the engine failed to arm look different
// here, which is the whole reason the column exists.
class BreakpointTable : public StdTable
{
    Q_OBJECT

public:
    struct Row
    {
        uint64_t address = 0;
        DbgBreakpointKind kind = DbgBreakpointKind_Software;
        uint32_t size = 0;      // watchpoints only
        bool enabled = true;    // what the user asked for
        bool effective = false; // whether the target is carrying it
    };

    explicit BreakpointTable(QWidget* parent = nullptr);

    void setBreakpoints(const std::vector<Row>& rows);

    // The address of the selected row, or 0 when there is no selection. Zero is not a usable
    // breakpoint address on either architecture, so it doubles as "nothing selected" without an
    // extra out-parameter.
    uint64_t selectedAddress() const;

    // What the table is showing, one "<address> <kind>[ size] <state>" entry per row -- the same
    // idea as RegisterTable::rows(), and for the same reason: an offscreen run can then assert
    // the list is populated with what was asked for rather than merely present.
    QStringList rows() const;

    // The human name of a kind, shared with whatever renders a stop reason so the two cannot
    // drift into calling the same thing different names.
    static QString kindName(DbgBreakpointKind kind);

signals:
    // Double-clicking a row asks for its enabled state to be flipped. The widget does not change
    // anything itself: only the engine knows whether the target accepted it, and a checkbox that
    // moves before the target does is a view that lies.
    void enabledToggleRequested(uint64_t address);

private slots:
    void onDoubleClicked();

private:
    std::vector<Row> mRows;
};
