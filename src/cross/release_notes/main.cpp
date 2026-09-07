#include <QFile>

#include <QDarkApplication.h>
#include <Gui/ReleaseNotesDialog.h>

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        puts("Usage: release_notes test.md");
        return EXIT_FAILURE;
    }

    QFile f(argv[1]);
    if(!f.open(QFile::ReadOnly))
    {
        puts("Failed to open file!");
        return EXIT_FAILURE;
    }
    auto markdown = f.readAll();

    QDarkApplication a(argc, argv);
    ReleaseNotesDialog d({});
    if(!d.setMarkdown(QString::fromUtf8(markdown), "https://github.com/x64dbg/x64dbg/issues/"))
    {
        puts("Failed to convert markdown!");
    }
    d.show();
    QDarkApplication::applyDarkTitleBar(&d);
    return d.exec();
}
