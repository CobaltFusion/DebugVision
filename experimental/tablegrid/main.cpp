#pragma warning( push )
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

#include "infra/cdbg.h"
#include "infra/exception.h"
#include "infra/logfunction.h"

#pragma warning( pop )

#pragma warning(disable : 4996)

// in Configuration: Debug 
// these settings are required for ABI compatibility:
// - set Configuration Properties->C / C++->Preprocessor->Preprocessor Definitions = > CSDEBUG
// - set Configuration Properties->C / C++->Code Generation->Runtime Library = > Multi - threaded DLL

// for assertions to work as usual include this file:
#include "cs_assert.h"

// any *.ui files in the project will be automatically compiled into corresponding $(OutDir)copperspice_generated\ui_*.h files.
// since $(OutDir)copperspice_generated in included in the compiler include path, this works:
#include "ui_mainwindow.h"
#include "ui_debugview.h"

#include <thread>
#include <array>
#include <chrono>
#include <iostream>

// needed to introduce usage of cs_main
#include "cs_main.h"
#include "cs_debugview.h"

using namespace std::chrono_literals;

namespace infra::dbgstream::output
{
	void write(const char* msg)
	{
		OutputDebugStringA(msg);
	}

	void write(const wchar_t* msg)
	{
		OutputDebugStringW(msg);
	}
}

class stringbuilder
{
public:

	template <typename T>
	stringbuilder& operator<<(const T& t)
	{
		m_ss << t;
		return *this;
	}

	operator std::string() const
	{
		return m_ss.str();
	}

	operator QString() const
	{
		std::string s = m_ss.str();
		return QString(s.cbegin(), s.cend());
	}

private:
	std::stringstream m_ss;
};

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
	item0->setToolTip(QString("Dit is a test of colomn 0"));
	QStandardItem* item2 = new QStandardItem(QString("pid"));
	item1->setToolTip(QString("Dit is a test of colomn 1"));
	QStandardItem* item3 = new QStandardItem(QString(message.c_str()));
	item2->setToolTip(QString("Dit is a test of colomn 2"));

	// Q1: how to select the whole line when selecting a message item
	// Q2: how to make the line number in the verticalHeader display correctly?

	
	model->setItem(newRowIndex, 0, item0);
	model->setItem(newRowIndex, 1, item1);
	model->setItem(newRowIndex, 2, item2);
	model->setItem(newRowIndex, 3, item3);
	view->selectRow(newRowIndex);
	//view->resizeRowToContents(newRowIndex); // crappy
	view->setRowHeight(newRowIndex, 12);
	//view->setContentsMargins(0, 0, 0, 0); // no having any effect?
	// view->layout()->setContentsMargins(0, 0, 0, 0); // crash; tableview has no layout?
}

QStandardItemModel* createModel(QTableView* tableView)
{
	QStandardItemModel* model = new QStandardItemModel(0, 0);
	tableView->setModel(model);		// set the model before doing changes like colomn width
	model->setHorizontalHeaderLabels({ "Line", "Time", "Process", "Message" }); // this will reset the colomnsizes
	
	tableView->setColumnWidth(1, 100);
	tableView->setColumnWidth(2, 100);
	tableView->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
	tableView->horizontalHeader()->setStretchLastSection(true);
	tableView->horizontalHeader()->setFrameStyle(QFrame::Shape::NoFrame);
	tableView->verticalHeader()->setFrameStyle(QFrame::Shape::NoFrame);
	tableView->verticalHeader()->setVisible(false);

	// alternating row colors
	tableView->setAlternatingRowColors(true);
	QPalette palette = tableView->palette();
	palette.setColor(QPalette::AlternateBase, QColor(210, 230, 255, 255));
	tableView->setPalette(palette);
	return model;
}

int exec(int argc, char* argv[])
{
	QApplication app(argc, argv);

	QMainWindow mainWindow;
	Ui::MainWindow window;
	window.setupUi(&mainWindow);
	window.verticalLayout->setMargin(0);

	//mainWindow.resize(800, 600);
	mainWindow.setWindowTitle("DebugVision v0.1 by Jan wilmans (c) 2019");

	auto menubar = mainWindow.menuBar();
	auto menu = menubar->addMenu("Options");
	auto flashAction = menu->addAction("Flash");
	auto assertAction = menu->addAction("Assert");
	auto sb = mainWindow.statusBar();
	mainWindow.show();

	// open debugview window -> move this into a class
	QMainWindow mainDebugview;
	Ui::debugview debugview;
	debugview.setupUi(&mainDebugview);
	mainDebugview.show();
	debugview.verticalLayout->setContentsMargins(0, 0, 0, 0);


	sb->showMessage("This message will disappear after 2 seconds", 2000);

	QObject::connect(flashAction, &QAction::triggered, &mainWindow, [sb] {
		sb->showMessage("Flash message that will disappear after 2 seconds", 2000);
	});

	QObject::connect(assertAction, &QAction::triggered, &mainWindow, [] {
		assert("This is not a valid assertion" && false);
	});

	// no effect
	//window.tabWidget->setContentsMargins(0, 0, 0, 0);
	//window.tab->setContentsMargins(0, 0, 0, 0);
	//window.tab_2->setContentsMargins(0, 0, 0, 0);
	//window.toolBox->setContentsMargins(0, 0, 0, 0);
	//window.toolBox->setFrameStyle(QFrame::Shape::NoFrame);
	window.verticalLayout->setContentsMargins(0, 0, 0, 0);
	window.verticalLayout_2->setContentsMargins(0, 0, 0, 0);
	//window.tableView->setContentsMargins(0, 0, 0, 0);
	//window.centralwidget->setContentsMargins(0, 0, 0, 0);


	window.tableView->setFont(QFont("Courier", 10));
	window.tableView->setShowGrid(false);
	window.tableView->setFrameStyle(QFrame::Shape::NoFrame);

	auto model = createModel(window.tableView);
	auto model2 = createModel(debugview.tableView);

	auto addMessage = [&](std::string msg) {
		addrow(msg, model, window.tableView);
		addrow(msg, model2, debugview.tableView);
	};

	QObject::connect(window.pushButton, &QPushButton::pressed, &mainWindow, [&]
	{
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
		addMessage("Mytestmessage");
	});

	return app.exec();
}

int cs_main(int argc, char *argv[])
{
	qInstallMsgHandler(CsDebugViewOutput);
	for (std::size_t i = 0; i < argc; ++i)
	{
		qDebug() << argv[i];
	}

	try
	{
		return exec(argc, argv);
	}
	catch (const std::exception & ex)
	{
		cdbg() << "Exception: " << ex.what() << "\n";
		throw;
	}
	catch (...)
	{
		cdbg() << "Exception: A non standard exception was thrown.\n";
		throw;
	}
	return -2;
}

