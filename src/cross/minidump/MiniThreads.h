#pragma once

#include <QSplitter>
#include <QTableWidget>
#include <vector>
#include <cstdint>

#include "Gui/RegistersView.h"

struct ThreadInfo
{
    uint32_t threadId = 0;
    uint64_t teb = 0;
    uint64_t cip = 0;
    bool is64 = false;
    REGDUMP_AVX512 registers;
};

class FileParser;

class MiniThreads : public QSplitter
{
    Q_OBJECT

public:
    explicit MiniThreads(QWidget* parent = nullptr);

    void loadThreads(const std::vector<ThreadInfo> & threads);

private slots:
    void onThreadSelected(int row, int column);

private:
    QTableWidget* mThreadList = nullptr;
    RegistersView* mRegistersView = nullptr;
    std::vector<ThreadInfo> mThreads;
};
