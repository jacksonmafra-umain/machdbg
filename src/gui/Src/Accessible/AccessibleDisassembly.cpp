// This file implements accessibility interface for Disassembly
#ifndef QT_NO_ACCESSIBILITY
#include "AccessibleDisassembly.h"
#include "Disassembly.h"
#include "Bridge.h"

AccessibleDisassembly::AccessibleDisassembly(QWidget* w) : AccessibleAbstractTableView(w)
{
}

AccessibleDisassembly::~AccessibleDisassembly()
{
}

Disassembly* AccessibleDisassembly::dis() const
{
    return dynamic_cast<Disassembly*>(m_tableView);
}

bool AccessibleDisassembly::isRowSelected(int row) const
{
    // Synchronize mInstBuffer before inspecting it. AT clients can call this
    // method directly, without first querying rowCount() or a cell.
    const int currentRowCount = rowCount();
    const auto disassembly = dis();
    return row >= 0
           && row < currentRowCount
           && row < disassembly->mInstBuffer.size()
           && disassembly->getInitialSelection() == disassembly->mInstBuffer.at(row).rva;
}

// TODO: multi-selection
int AccessibleDisassembly::selectedRowCount() const
{
    return selectedRows().size();
}

QList<int> AccessibleDisassembly::selectedRows() const
{
    // rowCount() synchronizes mInstBuffer with the current viewport before
    // accessibilitySelectedRow() searches it.
    const int currentRowCount = rowCount();
    const int selectedRow = dis()->accessibilitySelectedRow();
    if(selectedRow >= 0 && selectedRow < currentRowCount)
        return QList<int>({selectedRow});
    return QList<int>();
}

int AccessibleDisassembly::selectedCellCount() const
{
    return AccessibleAbstractTableView::selectedCellCount();
}

QList<QAccessibleInterface*> AccessibleDisassembly::selectedCells() const
{
    return AccessibleAbstractTableView::selectedCells();
}

static QString getDisassemblyMnemonicBrief(const Instruction_t & inst)
{
    char brief[MAX_STRING_SIZE] = "";
    QString mnem;
    for(const CapstoneTokenizer::SingleToken & token : inst.tokens.tokens)
    {
        if(token.type != CapstoneTokenizer::TokenType::Space && token.type != CapstoneTokenizer::TokenType::Prefix)
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

QString AccessibleDisassembly::getCellContent(int row, int col) const
{
    const Disassembly & d = *dis();
    const Instruction_t & inst = d.mInstBuffer.at(row);
    duint cur_addr = d.rvaToVa(inst.rva);
    switch(col)
    {
    case Disassembly::ColAddress:
    {
        QString label;
        return d.getAddrText(cur_addr, label, true);
    }
    case Disassembly::ColBytes:
    {
        QString bytes;
        RichTextPainter::htmlRichText(d.getRichBytes(inst, false), nullptr, bytes);
        return bytes;
    }
    case Disassembly::ColDisassembly:
    {
        QString disassembly;
        for(const auto & token : inst.tokens.tokens)
            disassembly += token.text;
        return disassembly;
    }
    case Disassembly::ColMnemonicBrief:
    {
        if(d.mShowMnemonicBrief)
            return getDisassemblyMnemonicBrief(inst);
        else
            return QString();
    }
    case Disassembly::ColComment:
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
        if(d.mShowMnemonicBrief && d.getColumnHidden(Disassembly::ColMnemonicBrief))
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