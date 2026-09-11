#include "MemoryMapTable.h"

#include <QFileInfo>
#include <QHash>

#include <mach/vm_prot.h>

namespace
{
    constexpr int kBaseColumn = 0;
    constexpr int kSizeColumn = 1;
    constexpr int kProtColumn = 2;
    constexpr int kTagColumn = 3;
    constexpr int kModuleColumn = 4;
}

MemoryMapTable::MemoryMapTable(QWidget* parent)
    : StdTable(parent)
{
    setAccessibleName(tr("Memory map"));
    setWindowTitle(tr("Memory map"));

    addColumnAt(140, tr("Base"), true);
    addColumnAt(100, tr("Size"), true);
    addColumnAt(60, tr("Prot"), true);
    addColumnAt(140, tr("Tag"), true);
    addColumnAt(220, tr("Module"), true);
}

QString MemoryMapTable::protectionText(const uint32_t protection)
{
    return QStringLiteral("%1%2%3")
        .arg((protection & VM_PROT_READ) != 0 ? QLatin1Char('r') : QLatin1Char('-'))
        .arg((protection & VM_PROT_WRITE) != 0 ? QLatin1Char('w') : QLatin1Char('-'))
        .arg((protection & VM_PROT_EXECUTE) != 0 ? QLatin1Char('x') : QLatin1Char('-'));
}

void MemoryMapTable::setRegions(const std::vector<DbgMemoryRegion>& regions,
                                const std::vector<DbgModule>& modules)
{
    QHash<quint64, QString> names;
    for(const DbgModule& module : modules)
        names.insert(module.base, QFileInfo(QString::fromUtf8(module.path)).fileName());

    mRegions = regions;
    setRowCount(mRegions.size());
    for(std::size_t i = 0; i < mRegions.size(); ++i)
    {
        const DbgMemoryRegion& region = mRegions[i];
        setCellContent(i, kBaseColumn, QStringLiteral("0x%1").arg(region.base, 0, 16));
        setCellContent(i, kSizeColumn, QStringLiteral("0x%1").arg(region.size, 0, 16));
        setCellContent(i, kProtColumn, protectionText(region.protection));
        setCellContent(i, kTagColumn, QString::fromUtf8(region.tagName ? region.tagName : ""));

        // Blank, not "none", for a region in no module: most of an address space is in none,
        // and a column of "none" would be noise where emptiness reads as absence already.
        QString module;
        if(region.moduleBase != 0)
        {
            module = names.value(region.moduleBase,
                                 QStringLiteral("0x%1").arg(region.moduleBase, 0, 16));
        }
        setCellContent(i, kModuleColumn, module);
    }
    reloadData();
}

QStringList MemoryMapTable::rows() const
{
    QStringList out;
    out.reserve(static_cast<int>(mRegions.size()));
    auto* self = const_cast<MemoryMapTable*>(this);
    for(std::size_t i = 0; i < mRegions.size(); ++i)
    {
        out.append(self->getCellContent(i, kBaseColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kSizeColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kProtColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kTagColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kModuleColumn));
    }
    return out;
}
