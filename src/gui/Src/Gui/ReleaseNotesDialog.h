#pragma once

#include <QDialog>
#include <functional>
#include "ImageTextBrowser.h"

namespace Ui
{
    class ReleaseNotesDialog;
}

class ReleaseNotesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReleaseNotesDialog(ImageTextBrowser::DownloadFn downloadFn, QWidget* parent = nullptr);
    ~ReleaseNotesDialog() override;
    bool setMarkdown(QString markdown, const QString & issueUrl);
    void setLabel(const QString & text);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    Ui::ReleaseNotesDialog* ui;
};
