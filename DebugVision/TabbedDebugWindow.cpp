
#include "TabbedDebugWindow.h"
#include "DynamicItemModel.h"

TabbedDebugWindow::TabbedDebugWindow(QMdiArea* mdiArea)
{
    model = new DynamicItemModel();
    //model->setHorizontalHeaderLabels({"Line", "Time", "Process", "Message"}); // this will reset the column widths

    //QIcon defaultIcon;
    //QIcon emptyStringIcon(QString(""));
    //QIcon notfoundIcon(QString("notfound"));
    //qwidget->setWindowIcon(notfoundIcon);

    //qDebug() << "is " << defaultIcon.isNull() << "\n";
    //qDebug() << "is " << emptyStringIcon.isNull() << "\n";
    //qDebug() << "is " << notfoundIcon.isNull() << "\n";

    qwidget->setWindowIcon(QIcon(QString("notfound"))); // remove Qt Icon

    tabbedview.setupUi(qwidget);
    mdiArea->addSubWindow(qwidget);
    //mdiArea->addSubWindow(qwidget);

    tabbedview.tableView->setModel(model);
    init(tabbedview.tableView);
    tabbedview.tableView->setFrameStyle(QFrame::Shape::NoFrame);
    tabbedview.tableView->setFont(QFont("Courier", 10));
    tabbedview.verticalLayout->setContentsMargins(0, 0, 0, 0);
    tabbedview.verticalLayout_2->setContentsMargins(0, 0, 0, 0);
    qwidget->show();
}

void TabbedDebugWindow::add(std::string msg)
{
    addrow(msg, model, tabbedview.tableView);
}

void init(QTableView* tableView)
{
    tableView->setColumnWidth(1, 100);
    tableView->setColumnWidth(2, 100);
    tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->verticalHeader()->setVisible(false); // we don't want to waste space on verticalHeaders

    // alternating row colors
    tableView->setAlternatingRowColors(true);
    QPalette palette = tableView->palette();
    palette.setColor(QPalette::AlternateBase, QColor(210, 230, 255, 255));
    tableView->setPalette(palette);
}

void addrow(std::string message, QAbstractItemModel* model, QTableView* view)
{
    using infra::stringbuilder;
    using namespace std::chrono;
    using clock = std::chrono::system_clock;
    auto now = clock::now();

    auto newRowIndex = model->rowCount();
    std::time_t now_c = clock::to_time_t(now);
    QStandardItem* item0 = new QStandardItem(QString("%1").formatArg(newRowIndex + 1));
    item0->setToolTip(QString("Dit is a test of colomn 0"));
    QStandardItem* item1 = new QStandardItem(stringbuilder() << std::put_time(std::localtime(&now_c), "%T"));
    item1->setToolTip(QString("Dit is a test of colomn 1"));
    QStandardItem* item2 = new QStandardItem(QString("pid"));
    item2->setToolTip(QString("Dit is a test of colomn 2"));
    QStandardItem* item3 = new QStandardItem(QString(message.c_str()));
    item3->setToolTip(QString("Dit is a test of colomn 3"));

    //model->setItem(newRowIndex, 0, item0);
    //model->setItem(newRowIndex, 1, item1);
    //model->setItem(newRowIndex, 2, item2);
    //model->setItem(newRowIndex, 3, item3);

    //view->selectRow(newRowIndex);         //this toggles the 'selected' state, not the 'active' line
    view->resizeRowToContents(newRowIndex); // crappy
    view->setRowHeight(newRowIndex, 12);
}


/*
<teasing you>  It would nice for you to learn how to use CS.

ansel  11:10 PM
It definitely would be a great chance for some real user experience. We don't have any specific input on how you would write such a thing. It might be worth looking at the Qt examples to see what they do.
11:12
I know that the video editor I use for our youtube videos, kdenlive  has a timeline control sort of like that but I'm not sure how tightly it might be coupled to the rest of the codebase. (It's a KDE / Qt app) (edited) 

Jan  11:13 PM
Ok, I can take a look for inspiration
11:14
What should I use if I just want to render the Widget completely custom?
11:14
Inherit from QWidget ?
11:15
how do I make a widget that can have children? or is that also completely free, up to the implementer (edited) 
11:16
What I wonder is, what existing component can I use
11:17
the 'lines' are movable so, I guess the layout-ing shouldn't have to be written by hand (edited) 
11:17
maybe a vertical-layout

barbara  11:17 PM
This is just one idea for you. Take a look at KS in the source for draw_area.h
11:18
DrawArea inherits from QWidget then it overrides paintEvent() (edited) 

Jan  11:19 PM
taking a look now
11:20
ok, that looks like a good start!

barbara  11:20 PM
I will note that this example was "lifted" from an older Qt4 example. The code may not be in the best shape. Might be worth seeing if this example has been improved in Qt 5.15.x samples. (edited) 

Jan  11:20 PM
nice and simple

barbara  11:21 PM
Just to be clear. It is perfectly legal to look at all source for the examples, even in 5.15.x  The code has a good license.

Jan  11:21 PM
if I want to be able to 'grab' a widget and "move" it (edited) 
11:21
how would I even do that

barbara  11:22 PM
Fridge magnets in KS.

Jan  11:22 PM
barbara: that's good also
11:22
I think I'll go study KS

barbara  11:22 PM
Good idea !
11:22
I walked every single example in Qt around 5.4 or so and took all the good ones
11:23
I should do this again with 5.15.x and see what else we can migrate.
:+1:
1

11:24
There are some wild things you can do in in the "Samples, Basic Drawing" and we play with this one a lot.

Jan  11:24 PM
I'll see if I see improvements in the draw_area example

barbara  11:25 PM
Glad to make changes to KS if it helps.  We did just make a few changes yesterday and pushed some changes
11:25
Fridge magnets was not simple to get right. Drag and drop pushes a lot of corners!
11:28
The other fun one we had a blast with was under Widgets, Tabs. This was new and not from Qt.  But it took us a full day to come up with the menu for the sandwich names.  Showing check boxes and radio buttons on one tab is a real use case.
*/
