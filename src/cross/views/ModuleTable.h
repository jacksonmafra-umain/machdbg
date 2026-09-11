#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The loaded modules, as data. Like RegisterTable and BreakpointTable, this widget is handed
// rows and knows nothing about the engine -- see RegisterTable.h for why these views are built
// on StdTable rather than on the vendored x86 ones.
//
// The slide has a column of its own rather than being folded into the address, because it is
// the number that turns a link-time address (what a map file or a disassembly listing shows)
// into a runtime one, and a user comparing the two needs to see it.
class ModuleTable : public StdTable
{
    Q_OBJECT

public:
    explicit ModuleTable(QWidget* parent = nullptr);

    void setModules(const std::vector<DbgModule>& modules);

    // What the table is showing, one "<name> <base> <slide>" entry per row -- the same idea as
    // RegisterTable::rows(): an offscreen run can then assert the list is populated rather than
    // merely present.
    QStringList rows() const;

private:
    std::vector<DbgModule> mModules;
};
