#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    QCoreApplication::setApplicationName("DebugVision");
    QCoreApplication::setApplicationVersion("0.1-qtdemo");

    MainWindow w;
    w.resize(800, 600);
    w.show();

    return a.exec();
}
