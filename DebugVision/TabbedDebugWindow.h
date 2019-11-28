#pragma once

#pragma warning(push)
#pragma warning(disable : 4100 4127 4251 4275 4244 4251 4312 4250)
#include <QtGui>
#pragma warning(pop)
#pragma warning(disable : 4996)

#include "ui_tabbedview.h"
#include "infra/stringbuilder.h"

void addrow(std::string message, QAbstractItemModel* model, QTableView* view);
void init(QTableView* tableView);

class TabbedDebugWindow
{
public:
    TabbedDebugWindow(QMdiArea* mdiArea);
    void add(std::string msg);

    QWidget* qwidget = new QWidget;

private:
    Ui::TabbedView tabbedview;
    QAbstractItemModel* model;
};
