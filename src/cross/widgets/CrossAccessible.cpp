#include "CrossAccessible.h"

#ifndef QT_NO_ACCESSIBILITY
#include <Accessible/AccessibleDisassembly.h>
#include <Accessible/AccessibleHexDump.h>
#include <Accessible/AccessibleRegistersView.h>
#include <Accessible/AccessibleStdTable.h>

QAccessibleInterface* crossAccessibleInterfaceFactory(const QString & classname, QObject* object)
{
    if(!object)
        return nullptr;

    if(classname == "Disassembly")
    {
        if(auto widget = dynamic_cast<Disassembly*>(object))
            return new AccessibleDisassembly(widget);
    }
    else if(classname == "HexDump")
    {
        if(auto widget = dynamic_cast<HexDump*>(object))
            return new AccessibleHexDump(widget);
    }
    else if(classname == "AbstractStdTable")
    {
        if(auto widget = dynamic_cast<AbstractStdTable*>(object))
            return new AccessibleStdTable(widget);
    }
    else if(classname == "RegistersView")
    {
        if(auto widget = dynamic_cast<RegistersView*>(object))
            return new AccessibleRegistersView(widget);
    }

    return nullptr;
}
#endif
