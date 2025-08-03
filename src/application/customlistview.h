#pragma once

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QStringList>
#include <QVector>
#include <QObject>

class CustomListViewWithHeaders : public QAbstractScrollArea
{
public:
    CustomListViewWithHeaders(QWidget *parent = nullptr)
        : QAbstractScrollArea(parent)
    {
        // Example headers
        headers = { "Name", "Age", "City" };

        // Example data
        data = {
            { "Alice", "30", "New York" },
            { "Bob", "25", "San Francisco" },
            { "Carol", "28", "Chicago" },
            { "Dave", "40", "Boston" },
            { "Eve", "35", "Seattle" },
            { "Frank", "50", "Austin" },
            { "Grace", "22", "Denver" },
            { "Heidi", "27", "Atlanta" },
            { "Ivan", "45", "Miami" },
            { "Judy", "31", "Los Angeles" },
        };

        rowHeight = 25;
        headerHeight = 30;
        columnWidths = { 100, 50, 120 };

        updateScrollArea();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), Qt::white);

        // Draw headers
        int x = 0;
        painter.setBrush(Qt::lightGray);
        painter.drawRect(0, 0, viewport()->width(), headerHeight);
        painter.setPen(Qt::black);
        for (int col = 0; col < headers.size(); ++col) {
            painter.drawText(x + 5, 20, headers[col]);
            x += columnWidths[col];
        }

        // Draw rows
        int firstRow = verticalScrollBar()->value();
        int visibleRows = viewport()->height() / rowHeight;
        int y = headerHeight;

        for (int row = firstRow; row < (firstRow + visibleRows + 1); ++row) {
            x = 0;
            for (int col = 0; col < data[row].size(); ++col) {
                painter.drawText(x + 5, y + 18, data[row%8][col]);
                x += columnWidths[col];
            }
            y += rowHeight;
        }
    }

    void resizeEvent(QResizeEvent *) override {
        updateScrollArea();
    }

private:
    QStringList headers;
    QVector<QStringList> data;
    QVector<int> columnWidths;
    int rowHeight;
    int headerHeight;

    void updateScrollArea() {
        auto contentHeight = rowHeight * data.size();
        verticalScrollBar()->setRange(0, qMax(0, data.size() * 1000));
        verticalScrollBar()->setPageStep(viewport()->height() / rowHeight);
        verticalScrollBar()->setSingleStep(3);
        viewport()->update();
    }
};
