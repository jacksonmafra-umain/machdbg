#pragma once
#include <QAccessible>

QAccessibleInterface* accessibleInterfaceFactory(const QString & classname, QObject* object);