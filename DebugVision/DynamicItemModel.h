#pragma once

#pragma warning(push)
#pragma warning(disable : 4100 4127 4251 4275 4244 4251 4312 4250)
#include <QtGui>
#pragma warning(pop)
#pragma warning(disable : 4996)

class DynamicItemModel : public QAbstractItemModel
{
public:
    using QAbstractItemModel::QAbstractItemModel;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
};
