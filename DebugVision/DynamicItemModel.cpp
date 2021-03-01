#include "DynamicItemModel.h"
#include "infra/cdbg.h"
#include "infra/stringbuilder.h"

QModelIndex DynamicItemModel::index(int row, int column, const QModelIndex& /*parent*/) const
{
    return createIndex(row, column);
}

QModelIndex DynamicItemModel::parent(const QModelIndex& /*child*/) const
{
    return {};
}

int DynamicItemModel::rowCount(const QModelIndex& /*parent*/) const
{
    return 2000000;
}

int DynamicItemModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 4;
}

QVariant DynamicItemModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::TextColorRole)
    {
        return QColor(((index.row() & 1) == 1) ? Qt::red : Qt::blue);
    }

    if (role == Qt::DisplayRole)
    {
        if (index.column() == 2)
        {
            return QString("Pa's wijze lynx bezag vroom het fikse aquaduct");
        }
        return index.row();
    }
    return {};
}
