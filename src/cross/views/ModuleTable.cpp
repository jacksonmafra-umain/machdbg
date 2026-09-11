#include "ModuleTable.h"

#include <QFileInfo>

namespace
{
    constexpr int kNameColumn = 0;
    constexpr int kBaseColumn = 1;
    constexpr int kSizeColumn = 2;
    constexpr int kSlideColumn = 3;
    constexpr int kPathColumn = 4;
}

ModuleTable::ModuleTable(QWidget* parent)
    : StdTable(parent)
{
    setAccessibleName(tr("Modules"));
    setWindowTitle(tr("Modules"));

    addColumnAt(180, tr("Name"), true);
    addColumnAt(140, tr("Base"), true);
    addColumnAt(100, tr("Size"), true);
    addColumnAt(100, tr("Slide"), true);
    addColumnAt(400, tr("Path"), true);
}

void ModuleTable::setModules(const std::vector<DbgModule>& modules)
{
    mModules = modules;
    setRowCount(mModules.size());
    for(std::size_t i = 0; i < mModules.size(); ++i)
    {
        const DbgModule& module = mModules[i];
        const QString path = QString::fromUtf8(module.path);
        // The last component, which is what a user reads a module list by. The full path stays
        // in its own column: on this platform it frequently names a file that does not exist,
        // because system dylibs come out of the shared cache, and hiding that would make the
        // list look like a list of files.
        setCellContent(i, kNameColumn, QFileInfo(path).fileName());
        setCellContent(i, kBaseColumn, QStringLiteral("0x%1").arg(module.base, 0, 16));
        setCellContent(i, kSizeColumn, QStringLiteral("0x%1").arg(module.size, 0, 16));
        setCellContent(i, kSlideColumn, QStringLiteral("0x%1").arg(module.slide, 0, 16));
        setCellContent(i, kPathColumn, path);
    }
    reloadData();
}

QStringList ModuleTable::rows() const
{
    QStringList out;
    out.reserve(static_cast<int>(mModules.size()));
    auto* self = const_cast<ModuleTable*>(this);
    for(std::size_t i = 0; i < mModules.size(); ++i)
    {
        out.append(self->getCellContent(i, kNameColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kBaseColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kSlideColumn) + QLatin1Char(' ') +
                   self->getCellContent(i, kPathColumn));
    }
    return out;
}
