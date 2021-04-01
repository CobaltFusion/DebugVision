#pragma once

#include "nowarnings_cs_gui.h"

#include <QBrush>
#include <QPen>
#include <QPixmap>
#include <QWidget>

class Timeline : public QWidget
{
    CS_OBJECT(Timeline)

public:
    enum Shape
    {
        Line,
        Points,
        Polyline,
        Polygon,
        Rect,
        RoundedRect,
        Ellipse,
        Arc,
        Chord,
        Pie,
        Path,
        Text
    };

    Timeline(QWidget* parent = nullptr);

    QSize minimumSizeHint() const;
    QSize sizeHint() const;

    CS_SLOT_1(Public, void setShape(Shape shape))
    CS_SLOT_2(setShape)

    CS_SLOT_1(Public, void setPen(const QPen& pen))
    CS_SLOT_2(setPen)

    CS_SLOT_1(Public, void setBrush(const QBrush& brush))
    CS_SLOT_2(setBrush)

    CS_SLOT_1(Public, void setAntialiased(bool antialiased))

    CS_SLOT_2(setAntialiased)

    CS_SLOT_1(Public, void setTransformed(bool transformed))
    CS_SLOT_2(setTransformed)

protected:
    void paintEvent(QPaintEvent* event);

private:
    Shape shape;
    QPen pen;
    QBrush brush;
    bool antialiased;
    bool transformed;
    QPixmap pixmap;
};

