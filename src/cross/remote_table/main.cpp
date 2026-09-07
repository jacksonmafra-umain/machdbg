#include <QDarkApplication.h>
#include <QThread>

#include "MainWindow.h"
#include "CrossAccessible.h"
#include "TableServer.h"

int main(int argc, char* argv[])
{
#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(crossAccessibleInterfaceFactory);
#endif

    QDarkApplication a(argc, argv);
    TableServer server;
    MainWindow w;
    w.show();
    QDarkApplication::applyDarkTitleBar(&w);
    return a.exec();
}
