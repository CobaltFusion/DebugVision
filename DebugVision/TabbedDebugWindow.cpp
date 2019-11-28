
#include "TabbedDebugWindow.h"

TabbedDebugWindow::TabbedDebugWindow(QMdiArea* mdiArea)
{
	model = new QStandardItemModel(0, 4);
	model->setHorizontalHeaderLabels({ "Line", "Time", "Process", "Message" }); // this will reset the column widths

	//QIcon defaultIcon;
	//QIcon emptyStringIcon(QString(""));
	//QIcon notfoundIcon(QString("notfound"));
	//qwidget->setWindowIcon(notfoundIcon);

	//qDebug() << "is " << defaultIcon.isNull() << "\n";
	//qDebug() << "is " << emptyStringIcon.isNull() << "\n";
	//qDebug() << "is " << notfoundIcon.isNull() << "\n";

	//qwidget.setWindowIcon(QIcon(QString("notfound")));

	tabbedview.setupUi(qwidget);
	mdiArea->addSubWindow(qwidget);

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

void addrow(std::string message, QStandardItemModel* model, QTableView* view)
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

	model->setItem(newRowIndex, 0, item0);
	model->setItem(newRowIndex, 1, item1);
	model->setItem(newRowIndex, 2, item2);
	model->setItem(newRowIndex, 3, item3);
	//view->selectRow(newRowIndex);				//this toggles the 'selected' state, not the 'active' line
	//view->resizeRowToContents(newRowIndex); // crappy
	view->setRowHeight(newRowIndex, 12);
}

void init(QTableView* tableView)
{
	tableView->setColumnWidth(1, 100);
	tableView->setColumnWidth(2, 100);
	tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
	tableView->horizontalHeader()->setStretchLastSection(true);
	tableView->verticalHeader()->setVisible(false);		// we don't want to waste space on verticalHeaders

	// alternating row colors
	tableView->setAlternatingRowColors(true);
	QPalette palette = tableView->palette();
	palette.setColor(QPalette::AlternateBase, QColor(210, 230, 255, 255));
	tableView->setPalette(palette);
}
