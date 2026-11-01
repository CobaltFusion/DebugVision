#include "Help.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QUrl>

bool openUserManual()
{
    QFile manual(":/help/readme.html");
    if (!manual.open(QIODevice::ReadOnly))
        return false;

    // outside the directory the self-contained executable unpacks into, which is removed when DebugVision exits
    QFile target(QDir::temp().filePath("debugvision-readme.html"));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    auto content = manual.readAll();
    bool written = target.write(content) == content.size();
    target.close();
    return written && QDesktopServices::openUrl(QUrl::fromLocalFile(target.fileName()));
}
