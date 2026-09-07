#include "MiniThreads.h"
#include "StringUtil.h"

#include <QHeaderView>
#include <QVBoxLayout>

MiniThreads::MiniThreads(QWidget* parent)
    : QSplitter(Qt::Horizontal, parent)
{
    setAccessibleName(tr("Threads"));

    // Thread list table
    mThreadList = new QTableWidget(this);
    mThreadList->setAccessibleName(tr("Threads"));
    mThreadList->setColumnCount(3);
    mThreadList->setHorizontalHeaderLabels({tr("Thread ID"), tr("TEB"), tr("CIP")});
    mThreadList->setSelectionBehavior(QAbstractItemView::SelectRows);
    mThreadList->setSelectionMode(QAbstractItemView::SingleSelection);
    mThreadList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mThreadList->horizontalHeader()->setStretchLastSection(true);
    mThreadList->verticalHeader()->setVisible(false);
    connect(mThreadList, &QTableWidget::cellClicked, this, &MiniThreads::onThreadSelected);

    // Registers view
    mRegistersView = new RegistersView(this);
    mRegistersView->ShowFPU(false);

    addWidget(mThreadList);
    addWidget(mRegistersView);
    setStretchFactor(0, 1);
    setStretchFactor(1, 2);
}

void MiniThreads::loadThreads(const std::vector<ThreadInfo> & threads)
{
    mThreads = threads;
    mThreadList->setRowCount((int)threads.size());

    for(int i = 0; i < (int)threads.size(); i++)
    {
        const auto & t = threads[i];

        auto* idItem = new QTableWidgetItem(QString("0x%1").arg(t.threadId, 0, 16));
        auto* tebItem = new QTableWidgetItem(ToHexString(t.teb));
        auto* cipItem = new QTableWidgetItem(ToHexString(t.cip));

        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        tebItem->setFlags(tebItem->flags() & ~Qt::ItemIsEditable);
        cipItem->setFlags(cipItem->flags() & ~Qt::ItemIsEditable);

        mThreadList->setItem(i, 0, idItem);
        mThreadList->setItem(i, 1, tebItem);
        mThreadList->setItem(i, 2, cipItem);
    }

    mThreadList->resizeColumnsToContents();

    // Select the first thread
    if(!threads.empty())
    {
        mThreadList->selectRow(0);
        onThreadSelected(0, 0);
    }
}

void MiniThreads::onThreadSelected(int row, int column)
{
    Q_UNUSED(column);
    if(row < 0 || row >= (int)mThreads.size())
        return;

    const auto & t = mThreads[row];
    mRegistersView->setRegisters(&t.registers);
}
