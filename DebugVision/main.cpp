#pragma warning(push)
#pragma warning(disable : 4251 4275 4244 4251 4312 4250)

#include <QMainWindow>
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QString>
#include <QTableView>
#include <QtGui>
#include <QHeaderView>
#include <QDebug>
#include <chrono>
#include <iomanip>

#pragma warning(pop)
#pragma warning(disable : 4996)

// any *.ui files in the project will be automatically compiled into corresponding $(OutDir)copperspice_generated\ui_*.h files.
// since $(OutDir)copperspice_generated in included in the compiler include path, this works:
#include "ui_mainwindow.h"
#include "ui_tabbedview.h"

#include "infra/cdbg.h"
#include "infra/exception.h"
#include "infra/logfunction.h"

#include <thread>
#include <array>
#include <chrono>
#include <iostream>

#include "cs_debugview.h"

using namespace std::chrono_literals;
using infra::stringbuilder;

void addrow(std::string message, QStandardItemModel * model, QTableView * view)
{
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

class TabbedDebugWindow
{
public:
	TabbedDebugWindow(QMdiArea* mdiArea)
	{
		model = new QStandardItemModel(0, 4);
		model->setHorizontalHeaderLabels({ "Line", "Time", "Process", "Message" }); // this will reset the column widths

		QIcon defaultIcon;
		QIcon emptyStringIcon(QString(""));
		QIcon notfoundIcon(QString("notfound"));
		qwidget->setWindowIcon(notfoundIcon);

		qDebug() << "is " << defaultIcon.isNull() << "\n";
		qDebug() << "is " << emptyStringIcon.isNull() << "\n";
		qDebug() << "is " << notfoundIcon.isNull() << "\n";

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

	void add(std::string msg)
	{
		addrow(msg, model, tabbedview.tableView);
	}

	QWidget* qwidget = new QWidget;
private:

	Ui::TabbedView tabbedview;
	QStandardItemModel* model;

};

void tileSubWindowsHorizontally(QMdiArea* mdiArea)
{
	QPoint position(0, 0);
	for (QMdiSubWindow * window: mdiArea->subWindowList()) {
		QRect rect(0, 0, mdiArea->width() / static_cast<int> (mdiArea->subWindowList().count()), mdiArea->height());
		window->setGeometry(rect);
		window->move(position);
		position.setX(position.x() + window->width());
	}
}

int exec(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QMainWindow mainWindow;
	Ui::MainWindow window;
	window.setupUi(&mainWindow);
	mainWindow.setWindowTitle("DebugVision v0.1 by Jan wilmans (c) 2019");

	auto menubar = mainWindow.menuBar();
	auto sb = mainWindow.statusBar();

	mainWindow.show();

	auto addviewAction = window.menuFile->addAction("New view");
	auto tileHorizontallyAction = window.menuOptions->addAction("Horizontal tile");

	QObject::connect(addviewAction, &QAction::triggered, &mainWindow, [&] {
		new TabbedDebugWindow(window.mdiArea);
	});

	QObject::connect(tileHorizontallyAction, &QAction::triggered, &mainWindow, [&] {
		tileSubWindowsHorizontally(window.mdiArea);
	});

	auto subwindow = std::make_unique<TabbedDebugWindow>(window.mdiArea);
	subwindow->qwidget->showMaximized();

	QObject::connect(window.pushButton, &QPushButton::pressed, &mainWindow, [&]
	{
		subwindow->add("Mytestmessage");
		subwindow->add("Mytestmessage");
		subwindow->add("Mytestmessage");
		subwindow->add("Mytestmessage");
		subwindow->add("Mytestmessage");
	});

	return app.exec();
}
