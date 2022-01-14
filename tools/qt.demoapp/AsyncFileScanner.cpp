#include "AsyncFileScanner.h"

#include <QDirIterator>
#include <QtConcurrent>

AsyncFileScanner::AsyncFileScanner()
{
    qDebug() << __func__;
    connect(&m_futureWatcher, &QFutureWatcher<void>::started, this, &AsyncFileScanner::scanStarted);
    connect(&m_futureWatcher, &QFutureWatcher<void>::finished, this, &AsyncFileScanner::scanFinished);
}

AsyncFileScanner::~AsyncFileScanner()
{
    qDebug() << __func__;
    stop();
}

void AsyncFileScanner::startFolderScan(const QString& rootDir)
{
    qDebug() << __func__;
    if (isScanInProgress())
    {
        qWarning() << "can't start a new file scanning: another scan is already in progress";
        return;
    }

    m_future = QtConcurrent::run([this, rootDir] {
        qDebug() << "async file scanner started";
        const QStringList files = performFileSearchStage(rootDir, {"*.log", "log*.txt"});
        if (!m_future.isCanceled())
        {
            performFileReadStage(files);
        }
        qDebug() << "async file scanner ended";
    });
    m_futureWatcher.setFuture(m_future);
}

void AsyncFileScanner::stop()
{
    qDebug() << __func__;
    if (m_future.isRunning())
    {
        qDebug() << "stopping async file scanner...";
        m_future.cancel();
        m_future.waitForFinished();
    }
}

QStringList AsyncFileScanner::performFileSearchStage(const QString& rootDir, const QStringList& patterns)
{
    Q_ASSERT(m_future.isRunning()); // we expect to run as an async task

    qDebug() << "searching for files of kind" << patterns;
    QStringList result;
    result.reserve(1'000);

    QDirIterator it(rootDir, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        if (m_future.isCanceled())
        {
            qDebug() << "file search was canceled";
            break;
        }

        QString filePath = it.next();
        result += filePath;
        emit foundNewFile(rootDir, filePath);
    }

    qDebug() << "end of file search, number of file found:" << result.size();
    return result;
}

static int countTextLinesInFile(const QString& filePath)
{
    QFile file{filePath};
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "failed to read file" << filePath;
        return 0;
    }

    QTextStream stream(&file);
    const QString text = stream.readAll();
    return text.isEmpty() ? 0 : 1 + text.count(QLatin1Char('\n'));
}

void AsyncFileScanner::performFileReadStage(const QStringList& files)
{
    qDebug() << "starting to read file content";
    for (const QString& filePath : files)
    {
        if (m_future.isCanceled())
        {
            qDebug() << "reading file content was canceled";
            break;
        }

        emit countedFileLineNumbers(filePath, countTextLinesInFile(filePath));
    }
}
