#include "Timeline.h"

#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>

Timeline::Timeline(QWidget* parent) :
    QWidget(parent)
{
    shape = Polygon;
    antialiased = false;
    transformed = false;

    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

QSize Timeline::minimumSizeHint() const
{
    return QSize(100, 100);
}

QSize Timeline::sizeHint() const
{
    return QSize(400, 200);
}

void Timeline::setShape(Shape value)
{
    shape = value;
    update();
}

void Timeline::setPen(const QPen& value)
{
    pen = value;
    update();
}

void Timeline::setBrush(const QBrush& value)
{
    brush = value;
    update();
}

void Timeline::setAntialiased(bool value)
{
    antialiased = value;
    update();
}

void Timeline::setTransformed(bool value)
{
    transformed = value;
    update();
}

void Timeline::paintEvent(QPaintEvent* /* event */)
{
    static const QPoint points[4] = {
        QPoint(10, 80),
        QPoint(20, 10),
        QPoint(80, 30),
        QPoint(90, 70)};

    QRect rect(10, 20, 80, 60);

    QPainterPath path;
    path.moveTo(20, 80);
    path.lineTo(20, 30);
    path.cubicTo(80, 0, 50, 50, 80, 80);

    int startAngle = 20 * 16;
    int arcLength = 120 * 16;

    QPainter painter(this);
    painter.setPen(pen);
    painter.setBrush(brush);
    if (antialiased)
    {
        painter.setRenderHint(QPainter::Antialiasing, true);
    }

    for (int x = 0; x < width(); x += 100)
    {
        for (int y = 0; y < height(); y += 100)
        {
            painter.save();
            painter.translate(x, y);

            if (transformed)
            {
                painter.translate(50, 50);
                painter.rotate(60.0);
                painter.scale(0.6, 0.9);
                painter.translate(-50, -50);
            }

            switch (shape)
            {
            case Line:
                painter.drawLine(rect.bottomLeft(), rect.topRight());
                break;
            case Points:
                painter.drawPoints(points, 4);
                break;
            case Polyline:
                painter.drawPolyline(points, 4);
                break;
            case Polygon:
                painter.drawPolygon(points, 4);
                break;
            case Rect:
                painter.drawRect(rect);
                break;
            case RoundedRect:
                painter.drawRoundedRect(rect, 25, 25, Qt::RelativeSize);
                break;
            case Ellipse:
                painter.drawEllipse(rect);
                break;
            case Arc:
                painter.drawArc(rect, startAngle, arcLength);
                break;
            case Chord:
                painter.drawChord(rect, startAngle, arcLength);
                break;
            case Pie:
                painter.drawPie(rect, startAngle, arcLength);
                break;
            case Path:
                painter.drawPath(path);
                break;
            case Text:
                painter.drawText(rect, Qt::AlignCenter, tr("Drawing Sample"));
                break;
            }

            painter.restore();
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(palette().dark().color());
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(0, 0, width() - 1, height() - 1));
}

