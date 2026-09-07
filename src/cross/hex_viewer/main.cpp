#include <QDarkApplication.h>
#include "MainWindow.h"
#include "CrossAccessible.h"

// https://www.qt.io/blog/dark-mode-on-windows-11-with-qt-6.5

int main(int argc, char* argv[])
{
#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(crossAccessibleInterfaceFactory);
#endif

    QDarkApplication app(argc, argv);

    MainWindow w;
    w.show();
    QDarkApplication::applyDarkTitleBar(&w);

    // Load the file provided on the command line.
    const auto arguments = app.arguments();
    if(arguments.size() > 1)
        w.loadFile(arguments.at(1));

    return app.exec();
}
