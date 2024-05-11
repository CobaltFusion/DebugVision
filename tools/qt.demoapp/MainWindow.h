#pragma once

#include "AsyncFileScanner.h"
#include "WidgetFileTextViewer.h"
#include "WidgetFileTreeView.h"

#include <QMainWindow>

class QAction;
class QProgressBar;

// Main window of the application
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();

    void clear();

private:
    void createMenus();
    void createStatusBar();
    void connectSignals();
    void onOpenFolder();

private:
    void closeEvent(QCloseEvent* event) final;

private:
    // UI elements
    QAction* m_actionOpenFolder = nullptr;
    QAction* m_actionCloseAll = nullptr;
    QProgressBar* m_progressBar = nullptr;
    WidgetFileTreeView m_logFileList;
    WidgetFileTextViewer m_logTextViewer;
    // non UI elements
    AsyncFileScanner m_fileScanner;
};
