#include "WidgetFileTextViewer.h"

static QString readFileContent(const QString& filePath)
{
    QFile file{filePath};
    if (!file.open(QIODevice::ReadOnly))
    {
        qCritical() << "failed to read file" << filePath;
        return "";
    }

    QTextStream stream(&file);
    return stream.readAll();
}

WidgetFileTextViewer::WidgetFileTextViewer(QWidget* parent) :
    QTextEdit(parent)
{
    setReadOnly(true);
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::NoWrap);
    setFontFamily("Courier New");
    setFontPointSize(10);
}

void WidgetFileTextViewer::displayFileContent(const QString& filePath)
{
    qDebug() << "displaying content of file" << filePath;
    clear();
    setPlainText(readFileContent(filePath));
}
