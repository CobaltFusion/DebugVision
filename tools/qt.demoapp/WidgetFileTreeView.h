#pragma once

#include <QTreeWidget>

// Widget responsible of displaying the list of available log files
class WidgetFileTreeView : public QTreeWidget
{
    Q_OBJECT
signals:
    void highlightedFileChanged(QString filePath);

public:
    WidgetFileTreeView(QWidget* parent = nullptr);

    void clear();
    void addRootFolder(const QString& rootPath);
    void addFile(const QString& rootPath, const QString& filePath);
    void setFileLineCount(const QString& filePath, int lineCount);

private:
    QTreeWidgetItem* ensureRootFolderItemExists(const QString& rootPath);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    std::unordered_map<QString, QTreeWidgetItem*> m_rootFolderItems;
    std::unordered_map<QString, QTreeWidgetItem*> m_filePathItems;
    std::optional<QString> m_highlightedFilePath;
};
