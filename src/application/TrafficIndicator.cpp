#include "TrafficIndicator.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {

constexpr int sampleIntervalMs = 100;
constexpr double linesFor100Percent = 10; // per sample
constexpr double silenceDecayMs = 1000;
constexpr size_t historyLength = 60;

constexpr int lightSize = 10;
constexpr int graphWidth = 160;
constexpr int spacing = 6;

// the colors of timelens
const QColor background(0x20, 0x28, 0x32);
const QColor border(0x3d, 0x48, 0x54);
const QColor grid(52, 229, 189, 9);
const QColor line(0x34, 0xe5, 0xbd);
const QColor lightOn(0x4a, 0xde, 0x80);
const QColor lightOff(0x14, 0x53, 0x2d, 128);

} // namespace

TrafficIndicator::TrafficIndicator(const dv::LineStore& store, QWidget* parent) :
    QWidget(parent),
    m_store(store),
    m_timer(new QTimer(this))
{
    setToolTip("Incoming lines: the light is on while lines arrive, the graph shows the lines per 100 ms over the last 6 seconds");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_timer->setInterval(sampleIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { sample(); });
}

QSize TrafficIndicator::sizeHint() const
{
    return QSize(lightSize + spacing + graphWidth + spacing, 22);
}

void TrafficIndicator::showEvent(QShowEvent*)
{
    // lines that arrived while hidden are not traffic
    m_lastSeq = m_store.endSeq();
    m_lastSample = std::chrono::steady_clock::now();
    m_timer->start();
}

void TrafficIndicator::hideEvent(QHideEvent*)
{
    m_timer->stop();
}

void TrafficIndicator::sample()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration<double, std::milli>(now - m_lastSample).count();
    m_lastSample = now;

    auto seq = m_store.endSeq();
    auto lines = seq - m_lastSeq;
    m_lastSeq = seq;

    m_active = lines > 0;
    if (m_active)
        m_level += (std::min(static_cast<double>(lines) / linesFor100Percent, 1.0) - m_level) * 0.5; // smooth incoming traffic
    else
        m_level *= std::exp(-elapsedMs / silenceDecayMs); // decay towards zero when traffic stops

    m_history.push_back(m_level);
    if (m_history.size() > historyLength)
        m_history.pop_front();
    update();
}

void TrafficIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF light(0.5, (height() - lightSize) / 2.0, lightSize, lightSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_active ? lightOn : lightOff);
    painter.drawEllipse(light);

    QRectF graph(lightSize + spacing + 0.5, 0.5, graphWidth - 1, height() - 1);
    painter.setPen(border);
    painter.setBrush(background);
    painter.drawRoundedRect(graph, 4, 4);

    painter.save();
    painter.setClipRect(graph.adjusted(1, 1, -1, -1));
    painter.setPen(grid);
    for (double x = graph.left() + 8; x < graph.right(); x += 8)
        painter.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
    for (double y = graph.top() + 8; y < graph.bottom(); y += 8)
        painter.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));

    if (m_history.size() >= 2)
    {
        auto inner = graph.adjusted(1, 2, -1, -2);
        QPainterPath path;
        for (size_t i = 0; i < m_history.size(); ++i)
        {
            QPointF p(inner.left() + static_cast<double>(i) * inner.width() / (historyLength - 1), inner.bottom() - std::min(1.0, m_history[i]) * inner.height());
            if (i == 0)
                path.moveTo(p);
            else
                path.lineTo(p);
        }
        painter.setPen(QPen(line, 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.restore();
}
