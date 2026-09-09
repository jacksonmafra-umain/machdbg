#include <QDarkApplication.h>

#include "CrossAccessible.h"
#include "MainWindow.h"

#include <QThread>

#include <cstdio>

// The accessibility factory is installed for the same reason milestone 1's samples install it:
// tests/accessibility drives a real window through AXUIElement, and that is how this app's
// register table is checked to be populated rather than merely present.
int main(int argc, char* argv[])
{
#ifndef QT_NO_ACCESSIBILITY
    QAccessible::installFactory(crossAccessibleInterfaceFactory);
#endif

    QDarkApplication app(argc, argv);

    MainWindow w;
    w.show();
    QDarkApplication::applyDarkTitleBar(&w);

    // A target on the command line launches immediately, which is what lets the accessibility
    // check, the self test and a screenshot run without driving a file dialog.
    const auto arguments = app.arguments();
    QString target;
    bool selfTest = false;
    for(int i = 1; i < arguments.size(); ++i)
    {
        if(arguments.at(i) == QStringLiteral("--selftest"))
            selfTest = true;
        else
            target = arguments.at(i);
    }

    if(!target.isEmpty())
        w.launch(target);

    if(!selfTest)
        return app.exec();

    // --selftest: run the real app, offscreen, against a real target, and report what the view
    // is showing. This is the half of milestone 3's proof that CI can run -- a screenshot proves
    // a populated view to a person and nothing to a job. The window is polled rather than waited
    // on with a signal because what is being checked is the *view's* state after the engine's
    // callbacks have been delivered and the widgets have been filled, which is a UI-thread
    // outcome; a five-second budget is far longer than a fixture takes to reach its first stop.
    QStringList report;
    bool passed = false;
    for(int attempt = 0; attempt < 100 && !passed; ++attempt)
    {
        app.processEvents();
        report.clear();
        passed = w.selfTest(&report);
        if(!passed)
            QThread::msleep(50);
    }

    for(const QString& line : report)
        std::fputs(qPrintable(line + QLatin1Char('\n')), stdout);
    std::fputs(passed ? "RESULT: the register table and the memory panel are populated\n"
                      : "RESULT: FAILED -- see the rows above\n", stdout);
    return passed ? 0 : 1;
}
