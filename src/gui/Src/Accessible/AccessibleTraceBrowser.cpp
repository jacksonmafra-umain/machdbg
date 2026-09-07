// This file implements accessibility interface for Disassembly
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleTraceBrowser.h"
#include "Tracer/TraceBrowser.h"
#include "Bridge.h"

AccessibleTraceBrowser::AccessibleTraceBrowser(QWidget* w) : AccessibleAbstractTableView(w)
{
}

AccessibleTraceBrowser::~AccessibleTraceBrowser()
{
}

TraceBrowser* AccessibleTraceBrowser::dis() const
{
    return dynamic_cast<TraceBrowser*>(m_tableView);
}

bool AccessibleTraceBrowser::isRowSelected(int row) const
{
    return row >= 0 && row < rowCount() && dis()->accessibilitySelectedRow() == row;
}

// TODO: multi-selection
int AccessibleTraceBrowser::selectedRowCount() const
{
    return selectedRows().size();
}

QList<int> AccessibleTraceBrowser::selectedRows() const
{
    const int selectedRow = dis()->accessibilitySelectedRow();
    if(selectedRow >= 0 && selectedRow < rowCount())
        return QList<int>({selectedRow});
    return QList<int>();
}

int AccessibleTraceBrowser::selectedCellCount() const
{
    return AccessibleAbstractTableView::selectedCellCount();
}

QList<QAccessibleInterface*> AccessibleTraceBrowser::selectedCells() const
{
    return AccessibleAbstractTableView::selectedCells();
}

static QString getDisassemblyMnemonicBrief(const Instruction_t & inst)
{
    char brief[MAX_STRING_SIZE] = "";
    QString mnem;
    for(const ZydisTokenizer::SingleToken & token : inst.tokens.tokens)
    {
        if(token.type != ZydisTokenizer::TokenType::Space && token.type != ZydisTokenizer::TokenType::Prefix)
        {
            mnem = token.text;
            break;
        }
    }
    if(mnem.isEmpty())
        mnem = inst.instStr;

    int index = mnem.indexOf(' ');
    if(index != -1)
        mnem.truncate(index);
    DbgFunctions()->GetMnemonicBrief(mnem.toUtf8().constData(), MAX_STRING_SIZE, brief);
    return QString::fromUtf8(brief);
}

QString AccessibleTraceBrowser::getCellContent(int row, int col) const
{
    TraceBrowser & d = *dis();
    TRACEINDEX index = row + d.getTableOffset();
    QString reason;
    auto & traceFile = *d.mTraceFile;
    Instruction_t inst = traceFile.Instruction(index);
    REGDUMP reg;
    reg = traceFile.Registers(index);
    duint cur_addr = reg.regcontext.cip;
    if(traceFile.isError(reason))
    {
        return QString();
    }
    switch(col)
    {
    case TraceBrowser::TableColumnIndex::Index:
        return traceFile.getIndexText(index);
    case TraceBrowser::TableColumnIndex::Address:
    {
        return d.getAddrText(cur_addr, nullptr, true);
    }
    case TraceBrowser::TableColumnIndex::Opcode:
    {
        QString bytes;
        RichTextPainter::htmlRichText(d.getRichBytes(inst), nullptr, bytes);
        return bytes;
    }
    case TraceBrowser::TableColumnIndex::Disassembly:
    {
        return inst.tokens.toString();
    }
    case TraceBrowser::TableColumnIndex::Registers:
    {
        return d.registersTokens(index).toString();
    }
    case TraceBrowser::TableColumnIndex::Memory:
    {
        return d.memoryTokens(index).toString();
    }
    case TraceBrowser::TableColumnIndex::Comments:
    {
        QString comment;
        bool autocomment;
        GetCommentFormat(cur_addr, comment, &autocomment);
        if(autocomment || comment.isEmpty())  // prefer label over auto-comment
        {
            char label[MAX_LABEL_SIZE];
            if(DbgGetLabelAt(cur_addr, SEG_DEFAULT, label))
            {
                comment = QString::fromUtf8(label);
            }
        }
        if(d.mShowMnemonicBrief)
        {
            comment += ' ' + getDisassemblyMnemonicBrief(inst);
        }
        return comment;
    }
    default:
        return QString();
    }
}

#endif