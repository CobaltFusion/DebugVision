#pragma once

#include <QColor>
#include <QHeaderView>
#include <QPainter>

#ifdef _WIN32
// The Windows style draws no line between the header and the rows, so it is drawn explicitly across the full width.
// Fusion (Linux) and the macOS style draw their own.
class UnderlinedHeaderView : public QHeaderView
{
public:
    using QHeaderView::QHeaderView;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QHeaderView::paintEvent(event);
        QPainter painter(viewport());
        painter.setPen(QColor(160, 160, 160));
        int y = viewport()->height() - 1;
        painter.drawLine(0, y, viewport()->width(), y);
    }
};
#else
using UnderlinedHeaderView = QHeaderView;
#endif
