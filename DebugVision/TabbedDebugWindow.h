#pragma once

#include "nowarnings_cs_gui.h"

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
