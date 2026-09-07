#pragma once

#ifndef QT_NO_ACCESSIBILITY
#include <QAccessible>

QAccessibleInterface* crossAccessibleInterfaceFactory(const QString & classname, QObject* object);
#endif
