#include "WidgetFileTreeView.h"

#include <QFileInfo>

// role used to store the absolute file path of the file associated to an item (if any)
constexpr int CustomRoleAbsFilePath = Qt::UserRole;

WidgetFileTreeView::WidgetFileTreeView(QWidget* parent) :
    QTreeWidget(parent)
{
    setRootIsDecorated(false);
    setHeaderLabels({tr("File path"), tr("Lines")});
    connect(this, &QTreeWidget::itemDoubleClicked, this, &WidgetFileTreeView::onItemDoubleClicked);
}

void WidgetFileTreeView::clear()
{
    QTreeWidget::clear();
    m_rootFolderItems.clear();
    m_filePathItems.clear();
    m_highlightedFilePath.reset();
}

void WidgetFileTreeView::addRootFolder(const QString& rootPath)
{
    ensureRootFolderItemExists(rootPath);
}

std::optional<QColor> itemColorForFileName(const QString& fileName)
{
    if (fileName.contains("error", Qt::CaseInsensitive))
    {
        return QColor(255, 219, 231); // very ligh red
    }
    return {};
}

void WidgetFileTreeView::addFile(const QString& rootPath, const QString& filePath)
{
    const QFileInfo finfo{filePath};
    Q_ASSERT(finfo.isFile());
    const QString absPath = finfo.absoluteFilePath();

    if (auto it = m_filePathItems.find(absPath); it == m_filePathItems.end())
    {
        const QDir rootDir(rootPath);
        const QString relPath = rootDir.relativeFilePath(absPath);

        QTreeWidgetItem* rootItem = ensureRootFolderItemExists(rootPath);
        auto item = new QTreeWidgetItem(rootItem, QStringList() << relPath);
        m_filePathItems[absPath] = item;
        item->setData(0, CustomRoleAbsFilePath, absPath);
        item->setToolTip(0, absPath);

        // adjust color if needed
        if (auto color = itemColorForFileName(finfo.fileName()))
        {
            item->setBackground(0, *color);
            item->setBackground(1, *color);
        }
    }
    else
    {
        qDebug() << "not adding file to the tree because it already exists";
    }
}

// simple formating helper for (not so) big numbers
static QString formatLineCount(int count)
{
    if (count > 999 && count < 1'000'000)
    {
        return QString("%1,%2").arg(count / 1'000).arg(count % 1'000, 3, 10, QLatin1Char('0'));
    }
    return QString::number(count);
}

void WidgetFileTreeView::setFileLineCount(const QString& filePath, int lineCount)
{
    const QFileInfo finfo{filePath};
    Q_ASSERT(finfo.isFile());
    const QString absPath = finfo.absoluteFilePath();

    if (auto it = m_filePathItems.find(absPath); it != m_filePathItems.end())
    {
        QTreeWidgetItem* item = it->second;
        item->setText(1, formatLineCount(lineCount));
    }
    else
    {
        qCritical() << "can't update file line count because it does not exist in the tree:" << filePath;
    }
}

QTreeWidgetItem* WidgetFileTreeView::ensureRootFolderItemExists(const QString& rootPath)
{
    const QFileInfo finfo{rootPath};
    Q_ASSERT(finfo.isDir());

    const QString absPath = finfo.absoluteFilePath();
    if (auto it = m_rootFolderItems.find(absPath); it != m_rootFolderItems.end())
    {
        // already created
        return it->second;
    }

    // not created yet: do it
    auto item = new QTreeWidgetItem(this, QStringList() << finfo.fileName());
    m_rootFolderItems[absPath] = item;
    item->setToolTip(0, absPath);
    item->setIcon(0, QPixmap(":/images/icons/folder-closed-16.png"));
    item->setExpanded(true);
    return item;
}

void WidgetFileTreeView::onItemDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    // get the file path associated to this item, if applicable
    const QString filePath = item->data(0, CustomRoleAbsFilePath).toString();
    if (filePath.isEmpty())
    {
        return;
    }

    QFont font = item->font(0);

    // clear the currently highlighted item, if any
    if (m_highlightedFilePath)
    {
        QTreeWidgetItem* highlightItem = m_filePathItems[*m_highlightedFilePath];
        Q_ASSERT(highlightItem != nullptr);
        font.setBold(false);
        highlightItem->setFont(0, font);
    }

    // highlight the new item
    m_highlightedFilePath = filePath;
    font.setBold(true);
    item->setFont(0, font);

    emit highlightedFileChanged(filePath);
}
