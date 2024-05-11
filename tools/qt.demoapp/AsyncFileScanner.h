#pragma once

// Type used to scan folders for log files via a background task
class AsyncFileScanner : public QObject
{
    Q_OBJECT
signals:
    void scanStarted();
    void scanFinished();
    void foundNewFile(QString rootDir, QString filePath);
    void countedFileLineNumbers(QString filePath, int lineCount);

public:
    AsyncFileScanner();
    ~AsyncFileScanner();

    [[nodiscard]] bool isScanInProgress() const
    {
        return m_future.isRunning();
    }

    void startFolderScan(const QString& rootDir);

    void stop();

private:
    QStringList performFileSearchStage(const QString& rootDir, const QStringList& patterns);
    void performFileReadStage(const QStringList& files);

private:
    bool m_isScanning = false;
    QFuture<void> m_future;
    QFutureWatcher<void> m_futureWatcher;
};
