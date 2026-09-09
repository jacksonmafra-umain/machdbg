#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The register view, as data. It is handed a descriptor table and a DbgRegisters and knows
// nothing else: no architecture, no struct fields, no engine. That is what spec section 5 means
// by "widgets never read struct fields" -- x0-x30 and rax-r15 are rows here, not two rendering
// paths.
//
// Why this exists rather than the vendored RegistersView (src/gui/Src/Gui/RegistersView.h): that
// view is a hardcoded x86 REGISTER_NAME enum over 3547 lines of .cpp. Extending it means paying
// a manual merge tax on every upstream sync, and feeding arm64 values into its x86 slots would
// put x0 in a row labelled rax -- a view that lies. Building on StdTable instead keeps upstream's
// rendering, selection, column sizing and accessibility (AccessibleStdTable, which
// tests/accessibility exercises) without inheriting its register model.
//
// Editing a cell emits registerEdited; this widget never calls the engine itself, which is what
// lets it be populated from a test, a sample app or the real debugger without changing.
class RegisterTable : public StdTable
{
    Q_OBJECT

public:
    explicit RegisterTable(QWidget* parent = nullptr);

    // Rebuilds the rows from a descriptor table. Called once the target's architecture is known
    // -- once per session in this milestone, since a target does not change architecture mid-run.
    void setDescriptors(const DbgRegisterDesc* descs, uint32_t count);

    // Refills the value column and marks the rows whose value differs from the previous call.
    // The marking is the feature that makes a register view usable while stepping: without it,
    // finding what an instruction changed means reading 34 rows.
    void setRegisters(const DbgRegisters& regs);

    // True if the row's value differed from the previous snapshot -- exposed for the accessibility
    // check in the sample app, which has no way to see a colour.
    bool rowChanged(uint32_t row) const;

    // What the table is showing, one "<name> <value>[ changed]" entry per row. This is what makes
    // the view checkable without a window server: an offscreen run can assert the rows are
    // *populated*, not merely present, which is the failure a screenshot review would catch by
    // eye and a headless CI job otherwise could not catch at all.
    QStringList rows() const;

signals:
    void registerEdited(const QString& name, uint64_t value);

private slots:
    void onDoubleClicked();

private:
    std::vector<DbgRegisterDesc> mDescs;
    std::vector<uint64_t> mPrevious;
    std::vector<bool> mChanged;
    bool mHavePrevious = false;
};
