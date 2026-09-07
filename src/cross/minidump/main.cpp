#include <QDarkApplication.h>
#include "MainWindow.h"
#include "CrossAccessible.h"

int main(int argc, char* argv[])
{
#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(crossAccessibleInterfaceFactory);
#endif

    QDarkApplication app(argc, argv);
    MainWindow w;
    w.show();
    QDarkApplication::applyDarkTitleBar(&w);

    // Load the dump provided on the command line.
    const auto arguments = app.arguments();
    if(arguments.size() > 1)
        w.loadFile(arguments.at(1));

    return app.exec();
}
