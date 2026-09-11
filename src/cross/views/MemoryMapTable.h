#pragma once

#include <cstdint>
#include <vector>

#include <QString>
#include <QStringList>

#include <MachBug/api/machbug_api.h>

#include "BasicView/StdTable.h"

// The target's address space, one row per region. Handed rows, like every view here.
//
// The module column is why this table is worth more than a list of addresses: a region's user
// tag does not say which image it belongs to -- an executable's own __TEXT is untagged --
// so the engine correlates it against the loaded images and this shows the answer.
class MemoryMapTable : public StdTable
{
    Q_OBJECT

public:
    explicit MemoryMapTable(QWidget* parent = nullptr);

    // `moduleNames` maps a module base to the name to show for it; a region whose module is not
    // in it shows its base instead, which is still more than nothing.
    void setRegions(const std::vector<DbgMemoryRegion>& regions,
                    const std::vector<DbgModule>& modules);

    QStringList rows() const;

    // VM_PROT_* as "rwx", with a dash for each bit that is not set. Static because the
    // breakpoint bench and any future view want the same three characters.
    static QString protectionText(uint32_t protection);

private:
    std::vector<DbgMemoryRegion> mRegions;
};
