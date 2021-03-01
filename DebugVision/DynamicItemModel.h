#pragma once

#include "nowarnings_cs_gui.h"

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
