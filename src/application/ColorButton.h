#pragma once

#include "dvcore/Filter.h"

#include <QPushButton>
#include <QString>

// a button that shows and edits a dv::Color; with an 'auto' description it also offers Auto
class ColorButton : public QPushButton
{
public:
    ColorButton(dv::Color color, const QString& autoDescription, QWidget* parent);

    dv::Color color() const { return m_color; }

private:
    void choose();
    void showColor();

    dv::Color m_color;
    QString m_autoDescription; // empty: Auto is not offered
};
