#include "nowarnings_cs_gui.h"

// any *.ui files in the project will be automatically compiled into corresponding $(OutDir)copperspice_generated\ui_*.h files.
// since $(OutDir)copperspice_generated is included in the compiler include path, this works:
#include "ui_mainwindow.h"

#include "infra/cdbg.h"
#include "infra/exception.h"
#include "infra/logfunction.h"

#include "cs_debugview.h"
#include "TabbedDebugWindow.h"

#include <chrono>
#include <iomanip>
#include <thread>
#include <array>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;


void tileSubWindowsHorizontally(QMdiArea* mdiArea)
{
    QPoint position(0, 0);
    for (QMdiSubWindow* window : mdiArea->subWindowList())
    {
        QRect rect(0, 0, mdiArea->width() / static_cast<int>(mdiArea->subWindowList().count()), mdiArea->height());
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
    mainWindow.setWindowTitle("DebugVision v0.2 by Jan wilmans (c) 2020");
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

    QObject::connect(window.pushButton, &QPushButton::pressed, &mainWindow, [&] {
        subwindow->add("Mytestmessage");
        subwindow->add("Mytestmessage");
        subwindow->add("Mytestmessage");
        subwindow->add("Mytestmessage");
        subwindow->add("Mytestmessage");
    });

    return app.exec();
}
