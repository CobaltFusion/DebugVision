#include "MainWindow.h"

#include <QSplitter>
#include <QFileDialog>
#include <QMenuBar>
#include <QProgressBar>
#include <QStatusBar>
#include <QProgressBar>
#include <QCloseEvent>

MainWindow::MainWindow()
{
    qDebug() << __func__;
    createMenus();
    createStatusBar();

    setWindowTitle(QString("%1 (%2)").arg(qApp->applicationName(), qApp->applicationVersion()));

    auto splitter = new QSplitter(this);
    splitter->addWidget(&m_logFileList);
    splitter->addWidget(&m_logTextViewer);
    splitter->setSizes(QList<int>() << 300 << 300);

    setCentralWidget(splitter);

    connectSignals();
}

void MainWindow::clear()
{
    m_logFileList.clear();
    m_logTextViewer.clear();
}

void MainWindow::createMenus()
{
    auto fileMenu = menuBar()->addMenu(tr("&File"));
    {
        m_actionOpenFolder = new QAction(tr("Open folder..."), this);
        m_actionOpenFolder->setStatusTip(tr("Search for log files in a selected folder."));
        m_actionOpenFolder->setShortcuts(QKeySequence::Open);
        connect(m_actionOpenFolder, &QAction::triggered, this, &MainWindow::onOpenFolder);
        fileMenu->addAction(m_actionOpenFolder);
    }
    fileMenu->addSeparator();
    {
        m_actionCloseAll = new QAction(tr("Clear all"), this);
        m_actionCloseAll->setStatusTip(tr("Clear all the elements."));
        connect(m_actionCloseAll, &QAction::triggered, this, &MainWindow::clear);
        fileMenu->addAction(m_actionCloseAll);
    }
    fileMenu->addSeparator();
    {
        auto action = new QAction(tr("Exit"), this);
        action->setStatusTip(tr("Exit the application"));
        action->setShortcuts(QKeySequence::Quit);
        connect(action, &QAction::triggered, this, &MainWindow::close);
        fileMenu->addAction(action);
    }
}

void MainWindow::createStatusBar()
{
    m_progressBar = new QProgressBar(statusBar());
    m_progressBar->setAlignment(Qt::AlignRight);
    m_progressBar->setTextVisible(false);
    m_progressBar->setMaximumWidth(200);
    statusBar()->addPermanentWidget(m_progressBar);
}

void MainWindow::connectSignals()
{
    // sync UI components with the status of the async file scanner
    connect(&m_fileScanner, &AsyncFileScanner::scanStarted, this, [this] {
        m_actionOpenFolder->setEnabled(false);
        m_actionCloseAll->setEnabled(false);
        m_progressBar->setRange(0, 0);
    });
    connect(&m_fileScanner, &AsyncFileScanner::scanFinished, this, [this] {
        m_actionOpenFolder->setEnabled(true);
        m_actionCloseAll->setEnabled(true);
        m_progressBar->setRange(0, 1);
    });
    // handle async file scanner results
    connect(&m_fileScanner, &AsyncFileScanner::foundNewFile, &m_logFileList, &WidgetFileTreeView::addFile);
    connect(&m_fileScanner, &AsyncFileScanner::countedFileLineNumbers, &m_logFileList, &WidgetFileTreeView::setFileLineCount);
    // sync the tree view with the text editor
    connect(&m_logFileList, &WidgetFileTreeView::highlightedFileChanged, &m_logTextViewer, &WidgetFileTextViewer::displayFileContent);
}

void MainWindow::onOpenFolder()
{
    if (m_fileScanner.isScanInProgress())
    {
        qWarning() << "can't open a new folder while file scanning is in progress";
        return;
    }

    const QString path = QFileDialog::getExistingDirectory(
        this,
        tr("Open folder"),
        "",
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty())
    {
        m_logFileList.addRootFolder(path);
        m_fileScanner.startFolderScan(path);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_fileScanner.stop();
    event->accept();
}
