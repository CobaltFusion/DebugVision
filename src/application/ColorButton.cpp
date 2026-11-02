#include "ColorButton.h"

#include <QColorDialog>
#include <QMenu>

ColorButton::ColorButton(dv::Color color, const QString& autoDescription, QWidget* parent) :
    QPushButton(parent),
    m_color(color),
    m_autoDescription(autoDescription)
{
    showColor();
    connect(this, &QPushButton::clicked, this, [this](bool) { choose(); });
}

void ColorButton::choose()
{
    if (!m_autoDescription.isEmpty())
    {
        QMenu menu(this);
        auto pick = menu.addAction("Choose color...");
        auto automatic = menu.addAction("Auto (" + m_autoDescription + ")");
        auto chosen = menu.exec(mapToGlobal(QPoint(0, height())));
        if (chosen == automatic)
        {
            m_color = dv::Color::autoColor();
            showColor();
            return;
        }
        if (chosen != pick)
            return;
    }
    auto initial = m_color.isAuto ? QColor(Qt::white) : QColor(m_color.red(), m_color.green(), m_color.blue());
    auto c = QColorDialog::getColor(initial, this);
    if (!c.isValid())
        return;
    m_color = dv::Color::fromRgb(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
    showColor();
}

void ColorButton::showColor()
{
    if (m_color.isAuto)
    {
        setText("Auto");
        setStyleSheet(QString());
        return;
    }
    setText(QString());
    setStyleSheet(QString("background-color: rgb(%1,%2,%3)").formatArg(QString::number(m_color.red())).formatArg(QString::number(m_color.green())).formatArg(QString::number(m_color.blue())));
}
