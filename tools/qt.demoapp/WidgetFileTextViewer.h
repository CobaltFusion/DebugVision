#pragma once

#include <QTextEdit>

// Widget responsible of displaying the content of a log file
class WidgetFileTextViewer : public QTextEdit
{
public:
    WidgetFileTextViewer(QWidget* parent = nullptr);

    void displayFileContent(const QString& filePath);
};
