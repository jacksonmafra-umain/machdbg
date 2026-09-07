#ifndef QT_NO_ACCESSIBILITY
#include "Accessible.h"
#include "AccessibleRegistersView.h"
#include "AccessibleAbstractTableView.h"
#include "AccessibleDisassembly.h"
#include "AccessibleHexDump.h"
#include "AccessibleStdTable.h"
#include "AccessibleTraceBrowser.h"

QAccessibleInterface* accessibleInterfaceFactory(const QString & classname, QObject* object)
{
    QAccessibleInterface* ptr = nullptr;
    if(!object)
        return nullptr;
    if((classname == "Disassembly") && dynamic_cast<Disassembly*>(object) != nullptr)
    {
        ptr = new AccessibleDisassembly(dynamic_cast<QWidget*>(object));
    }
    else if((classname == "HexDump") && dynamic_cast<HexDump*>(object) != nullptr)
    {
        ptr = new AccessibleHexDump(dynamic_cast<HexDump*>(object));
    }
    else if((classname == "AbstractStdTable") && dynamic_cast<AbstractStdTable*>(object) != nullptr)
    {
        ptr = new AccessibleStdTable(dynamic_cast<QWidget*>(object));
    }
    else if((classname == "RegistersView") && dynamic_cast<RegistersView*>(object) != nullptr)
    {
        ptr = new AccessibleRegistersView(dynamic_cast<QWidget*>(object));
    }
    else if((classname == "TraceBrowser") && dynamic_cast<TraceBrowser*>(object) != nullptr)
    {
        ptr = new AccessibleTraceBrowser(dynamic_cast<QWidget*>(object));
    }
    return ptr;
}

#endif