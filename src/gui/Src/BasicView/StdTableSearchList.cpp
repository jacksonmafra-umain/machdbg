#include "StdTableSearchList.h"
#include "StdIconTable.h"

void StdTableSearchList::filter(const QString & filter, FilterType type, duint startColumn)
{
    StdIconTable* mSearchIconList = qobject_cast<StdIconTable*>(mSearchList);
    StdIconTable* mIconList = qobject_cast<StdIconTable*>(mList);

    auto rows = mList->getRowCount();
    auto columns = mList->getColumnCount();

    // collect matching row indices
    std::vector<duint> matchingRows;

    for(duint row = 0; row < rows; row++)
    {
        if(rowMatchesFilter(filter, type, row, startColumn))
            matchingRows.push_back(row);
    }

    // resize once with exact size
    mSearchList->setRowCount(matchingRows.size());

    // populate searchList with matched results
    for(duint searchListRow = 0; searchListRow < matchingRows.size(); searchListRow++)
    {
        duint listRow = matchingRows[searchListRow];
        for(duint column = 0; column < columns; column++)
        {
            mSearchList->setCellContent(searchListRow, column,
                                        mList->getCellContent(listRow, column),
                                        mList->getCellUserdata(listRow, column));
        }
        if(mSearchIconList && mIconList)
            mSearchIconList->setRowIcon(searchListRow, mIconList->getRowIcon(listRow));
    }
}
