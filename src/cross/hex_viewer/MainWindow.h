#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>

#include <QMainWindow>

#include "MiniHexDump.h"
#include "Navigation.h"
#include "File.h"
#include "CodeEditor.h"
#include "PatternHighlighter.h"
#include "PatternLanguage.h"
#include <Gui/TypeWidget.h>
#include <QTextBrowser>
#include "DataTable.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void loadFile(const QString & path);

private slots:
    void onButtonRunPressed();
    void onLogAnchorClicked(const QUrl & url);
    void gotoHexDump(duint address, duint size);

    void on_action_Load_file_triggered();

private:
    void setupNavigation();
    void setupWidgets();
    void logHandler(LogLevel level, const char* message);
    void compileError(const CompileError & error);
    void evalError(const EvalError & error);
    bool typeValueCallback(const TYPEDESCRIPTOR* type, char* dest, size_t* destCount);
    TreeNode typeVisit(TreeNode parent, const VisitInfo & info);
    void typeClear();

private:
    Ui::MainWindow* ui = nullptr;
    Navigation* mNavigation = nullptr;
    MiniHexDump* mHexDump = nullptr;
    File* mFile = nullptr;
    CodeEditor* mCodeEditor = nullptr;
    PatternHighlighter* mHighlighter = nullptr;
    QTextBrowser* mLogBrowser = nullptr;
    TypeWidget* mTypeWidget = nullptr;
    DataTable* mDataTable = nullptr;
    QTabWidget* mStructTabs = nullptr;

    int mNextId = 100;
    uint64_t mCurrentBase = 0;
    std::vector<char> mStringPool;
    std::map<int, std::pair<size_t, size_t>> mNames;
};
#endif // MAINWINDOW_H
