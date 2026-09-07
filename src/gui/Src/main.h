#pragma once

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QMessageBox>
#include "Bridge.h"
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
#include <QAbstractNativeEventFilter>
#endif

class MyApplication : public QApplication
{
    Q_OBJECT
public:
    MyApplication(int & argc, char** argv);
    bool notify(QObject* receiver, QEvent* event) Q_DECL_OVERRIDE;
#if QT_VERSION < QT_VERSION_CHECK(5,0,0)
    bool winEventFilter(MSG* message, long* result) Q_DECL_OVERRIDE;
    static bool globalEventFilter(void* message);
#endif
};

int main(int argc, char* argv[]);
extern char gCurrentLocale[MAX_SETTING_SIZE];

struct TranslatedStringStorage
{
    char Data[4096];
};

extern thread_local TranslatedStringStorage TLS_TranslatedString;

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
class MyEventFilter : public QAbstractNativeEventFilter
{
protected:
    bool nativeEventFilter(const QByteArray & eventType, void* message, long* result) override
    {
        if(eventType == "windows_dispatcher_MSG")
            return DbgWinEventGlobal((MSG*)message);
        if(eventType == "windows_generic_MSG")
            return DbgWinEvent((MSG*)message, result);
        return false;
    }
};
#endif // QT_VERSION
