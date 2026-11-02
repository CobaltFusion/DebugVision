#include "TimelineWidget.h"

#include "Text.h"

#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int rulerHeight = 22;
constexpr int markerArea = 14; // above the bars of a lane: the heads of the event markers
constexpr int rowHeight = 12;  // one row of bars
constexpr int laneGap = 6;
constexpr int minPlotWidth = 420; // below this the label column is hidden
constexpr int64_t minViewWidth = 1000;                       // 1 us
constexpr int64_t maxViewWidth = 1'000'000'000'000'000'000; // ~31 years

QString formatDuration(int64_t ns)
{
    auto v = static_cast<double>(ns);
    if (ns < 1000)
        return QString::number(ns) + " ns";
    if (ns < 1'000'000)
        return QString::number(v / 1e3, 'g', 4) + QString::fromUtf8(" \xC2\xB5s");
    if (ns < 1'000'000'000)
        return QString::number(v / 1e6, 'g', 4) + " ms";
    return QString::number(v / 1e9, 'g', 6) + " s";
}

QColor textColorOn(const QColor& background)
{
    return background.lightness() > 140 ? QColor(Qt::black) : QColor(Qt::white);
}

} // namespace

TimelineWidget::TimelineWidget(const dv::TimelineModel& model, const int64_t& timeBase, QWidget* parent) :
    QWidget(parent),
    m_model(model),
    m_timeBase(timeBase)
{
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    rulesChanged();
}

QSize TimelineWidget::sizeHint() const
{
    return QSize(600, contentHeight());
}

void TimelineWidget::rulesChanged()
{
    m_ruleColors.clear();
    dv::ColorGenerator generator{0.6, 0.85};
    for (auto& rule : m_model.rules())
    {
        auto c = rule.color.isAuto ? generator.next() : rule.color;
        m_ruleColors.push_back(QColor(c.red(), c.green(), c.blue()));
    }
    update();
}

void TimelineWidget::modelChanged(bool follow)
{
    if (m_model.empty())
    {
        m_fitted = false;
    }
    else if (!m_fitted)
    {
        fitAll();
        m_fitted = true;
    }
    else if (m_live && follow)
    {
        followEnd();
    }
    if (minimumHeight() != contentHeight())
        setMinimumHeight(contentHeight());
    update();
}

void TimelineWidget::setCursorNs(int64_t ns)
{
    if (ns == m_cursorNs)
        return;
    m_cursorNs = ns;
    update();
}

// ---------------------------------------------------------------------------------------------------- geometry

int TimelineWidget::labelWidth() const
{
    if (width() < minPlotWidth)
        return 0;
    QFontMetrics fm(font());
    int w = 60;
    for (auto& lane : m_model.lanes())
        w = std::max(w, fm.width(toQString(lane.name)) + 20);
    return std::min(w, 200);
}

int TimelineWidget::plotWidth() const
{
    return std::max(1, width() - plotLeft());
}

int TimelineWidget::laneHeight(const dv::TimelineLane& lane) const
{
    return markerArea + static_cast<int>(lane.depthCount()) * rowHeight + laneGap;
}

int TimelineWidget::contentHeight() const
{
    int h = rulerHeight;
    for (auto& lane : m_model.lanes())
        h += laneHeight(lane);
    return std::max(h, rulerHeight + 40);
}

double TimelineWidget::xOf(int64_t ns) const
{
    return plotLeft() + static_cast<double>(ns - m_viewStart) * plotWidth() / static_cast<double>(m_viewWidth);
}

int64_t TimelineWidget::nsAt(double x) const
{
    return m_viewStart + static_cast<int64_t>((x - plotLeft()) * static_cast<double>(m_viewWidth) / plotWidth());
}

int64_t TimelineWidget::spanEnd(const dv::TimelineSpan& span) const
{
    return span.endNs >= 0 ? span.endNs : std::max(m_model.lastNs(), span.beginNs);
}

QColor TimelineWidget::ruleColor(uint32_t rule) const
{
    return rule < m_ruleColors.size() ? m_ruleColors[rule] : QColor(Qt::gray);
}

// ---------------------------------------------------------------------------------------------------- view range

void TimelineWidget::fitAll()
{
    auto width = std::max<int64_t>(m_model.lastNs() - m_model.firstNs(), 1'000'000);
    m_viewWidth = std::clamp<int64_t>(width + width / 20, minViewWidth, maxViewWidth);
    m_viewStart = m_model.firstNs() - m_viewWidth / 40;
}

void TimelineWidget::followEnd()
{
    m_viewStart = m_model.lastNs() - m_viewWidth + m_viewWidth / 50;
}

void TimelineWidget::zoomAround(double x, double factor)
{
    auto at = nsAt(x);
    auto width = static_cast<int64_t>(static_cast<double>(m_viewWidth) * factor);
    m_viewWidth = std::clamp(width, minViewWidth, maxViewWidth);
    m_viewStart = at - static_cast<int64_t>((x - plotLeft()) * static_cast<double>(m_viewWidth) / plotWidth());
    m_live = false;
    update();
}

void TimelineWidget::panBy(double dx)
{
    m_viewStart -= static_cast<int64_t>(dx * static_cast<double>(m_viewWidth) / plotWidth());
    m_live = false;
    update();
}

// ---------------------------------------------------------------------------------------------------- painting

void TimelineWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    auto palette = this->palette();
    painter.fillRect(rect(), palette.color(QPalette::Base));

    if (m_model.rules().empty() || m_model.lanes().empty())
    {
        drawRuler(painter);
        painter.setPen(palette.color(QPalette::Text));
        auto text = m_model.rules().empty() ? QString("Add rules with View > Timeline Rules...") : QString("No lines in this view match the timeline rules");
        painter.drawText(rect().adjusted(0, rulerHeight, 0, 0), static_cast<int>(Qt::AlignCenter), text);
        return;
    }

    int top = rulerHeight;
    for (size_t i = 0; i < m_model.lanes().size(); ++i)
    {
        auto& lane = m_model.lanes()[i];
        auto h = laneHeight(lane);
        if (i % 2 == 1)
            painter.fillRect(QRect(0, top, width(), h), palette.color(QPalette::AlternateBase));
        drawLane(painter, lane, top);
        top += h;
    }
    drawRuler(painter);

    if (m_measureFrom != INT64_MIN && m_measureTo != INT64_MIN && m_measureFrom != m_measureTo)
    {
        auto x0 = static_cast<int>(xOf(std::min(m_measureFrom, m_measureTo)));
        auto x1 = static_cast<int>(xOf(std::max(m_measureFrom, m_measureTo)));
        auto highlight = palette.color(QPalette::Highlight);
        highlight.setAlpha(60);
        painter.fillRect(QRect(x0, rulerHeight, std::max(1, x1 - x0), height() - rulerHeight), highlight);
        painter.setPen(palette.color(QPalette::Highlight));
        painter.drawLine(x0, rulerHeight, x0, height());
        painter.drawLine(x1, rulerHeight, x1, height());
        auto label = formatDuration(std::abs(m_measureTo - m_measureFrom));
        QFontMetrics fm(font());
        QRect box(x1 + 4, rulerHeight + 2, fm.width(label) + 8, fm.height() + 2);
        if (box.right() > width())
            box.moveRight(x0 - 4);
        painter.fillRect(box, palette.color(QPalette::ToolTipBase));
        painter.setPen(palette.color(QPalette::ToolTipText));
        painter.drawText(box, static_cast<int>(Qt::AlignCenter), label);
    }

    if (m_cursorNs != INT64_MIN)
    {
        auto x = static_cast<int>(xOf(m_cursorNs));
        if (x >= plotLeft())
        {
            painter.setPen(QPen(QColor(220, 40, 40), 1));
            painter.drawLine(x, rulerHeight, x, height());
        }
    }
}

void TimelineWidget::drawRuler(QPainter& painter)
{
    auto palette = this->palette();
    painter.fillRect(QRect(0, 0, width(), rulerHeight), palette.color(QPalette::Window));
    painter.setPen(palette.color(QPalette::Mid));
    painter.drawLine(0, rulerHeight - 1, width(), rulerHeight - 1);
    if (m_model.empty())
        return;

    // 1-2-5 steps, at least 90 pixels apart
    auto minStep = static_cast<double>(m_viewWidth) / plotWidth() * 90.0;
    auto magnitude = std::pow(10.0, std::floor(std::log10(std::max(minStep, 1.0))));
    double step = magnitude;
    for (double m : {1.0, 2.0, 5.0, 10.0})
    {
        step = magnitude * m;
        if (step >= minStep)
            break;
    }
    auto decimals = std::clamp(static_cast<int>(-std::floor(std::log10(step / 1e9))), 0, 9);
    auto first = std::ceil(static_cast<double>(m_viewStart - m_timeBase) / step) * step;
    auto viewEnd = static_cast<double>(m_viewStart - m_timeBase + m_viewWidth);
    auto grid = palette.color(QPalette::Mid);
    grid.setAlpha(60);
    for (double t = first; t <= viewEnd; t += step)
    {
        auto x = static_cast<int>(xOf(m_timeBase + static_cast<int64_t>(t)));
        if (x < plotLeft())
            continue;
        painter.setPen(grid);
        painter.drawLine(x, rulerHeight, x, height());
        painter.setPen(palette.color(QPalette::WindowText));
        painter.drawLine(x, rulerHeight - 5, x, rulerHeight - 1);
        painter.drawText(x + 3, rulerHeight - 7, QString::number(t / 1e9, 'f', decimals) + " s");
    }
}

void TimelineWidget::drawLane(QPainter& painter, const dv::TimelineLane& lane, int top)
{
    auto palette = this->palette();
    auto h = laneHeight(lane);
    QFontMetrics fm(font());

    if (plotLeft() > 0)
    {
        painter.setPen(palette.color(QPalette::Text));
        painter.drawText(QRect(4, top, plotLeft() - 8, h - laneGap), static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
            fm.elidedText(toQString(lane.name), Qt::ElideRight, plotLeft() - 8));
    }
    painter.setClipRect(QRect(plotLeft(), top, plotWidth(), h));

    auto viewEnd = m_viewStart + m_viewWidth;
    auto laneIndex = static_cast<size_t>(&lane - m_model.lanes().data());

    // spans: those that can reach into the view, skipping bars that would land on pixels already drawn
    std::vector<double> drawnUntil(lane.depthCount(), -1e9);
    auto drawSpan = [&](const dv::TimelineSpan& span) {
        auto end = spanEnd(span);
        if (end < m_viewStart || span.beginNs > viewEnd)
            return;
        auto x0 = std::max(xOf(span.beginNs), static_cast<double>(plotLeft() - 1));
        auto x1 = std::min(xOf(end), static_cast<double>(width() + 1));
        if (x1 - x0 < 1.0 && x1 <= drawnUntil[span.depth])
            return;
        drawnUntil[span.depth] = std::max(drawnUntil[span.depth], x1);
        auto color = ruleColor(span.rule);
        if (span.endNs < 0)
            color.setAlpha(110); // still open
        QRectF bar(x0, top + markerArea + static_cast<int>(span.depth) * rowHeight, std::max(x1 - x0, 1.0), rowHeight - 2);
        painter.fillRect(bar, color);
        if (bar.width() > 40)
        {
            painter.setPen(textColorOn(color));
            auto name = toQString(m_model.rules()[span.rule].displayName());
            painter.drawText(bar.adjusted(3, 0, -3, 0), static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                fm.elidedText(name, Qt::ElideRight, static_cast<int>(bar.width()) - 6));
        }
    };
    auto from = std::partition_point(lane.spans.begin(), lane.spans.end(),
        [&](const dv::TimelineSpan& s) { return s.beginNs < m_viewStart - lane.maxDuration; });
    for (auto it = from; it != lane.spans.end() && it->beginNs <= viewEnd; ++it)
        drawSpan(*it);
    auto fromIndex = static_cast<size_t>(from - lane.spans.begin());
    for (auto [l, s] : m_model.openSpans())
        if (l == laneIndex && s < fromIndex)
            drawSpan(lane.spans[s]); // open spans that began before the closed ones in view

    // events
    int lastX = INT_MIN;
    uint32_t lastRule = UINT32_MAX;
    auto first = std::partition_point(lane.events.begin(), lane.events.end(), [&](const dv::TimelineEvent& e) { return e.ns < m_viewStart; });
    for (auto it = first; it != lane.events.end() && it->ns <= viewEnd; ++it)
    {
        auto x = static_cast<int>(std::lround(xOf(it->ns)));
        if (x == lastX && it->rule == lastRule)
            continue;
        lastX = x;
        lastRule = it->rule;
        drawMarker(painter, m_model.rules()[it->rule].shape, x, top, top + h - laneGap, ruleColor(it->rule));
    }

    painter.setClipping(false);
    painter.setPen(palette.color(QPalette::Midlight));
    painter.drawLine(0, top + h - 1, width(), top + h - 1);
}

void TimelineWidget::drawMarker(QPainter& painter, dv::MarkerShape shape, int x, int top, int bottom, const QColor& color)
{
    auto headY = top + markerArea / 2;
    painter.setPen(QPen(color.darker(130), 1));
    painter.drawLine(x, headY, x, bottom);
    painter.setBrush(color);
    switch (shape)
    {
    case dv::MarkerShape::Lollipop: painter.drawEllipse(QPoint(x, headY), 4, 4); break;
    case dv::MarkerShape::Square: painter.drawRect(x - 4, headY - 4, 8, 8); break;
    case dv::MarkerShape::Triangle:
    {
        QPolygon triangle;
        triangle << QPoint(x - 5, headY - 4) << QPoint(x + 5, headY - 4) << QPoint(x, headY + 5);
        painter.drawPolygon(triangle);
        break;
    }
    }
    painter.setBrush(Qt::NoBrush);
}

// ---------------------------------------------------------------------------------------------------- hit testing

TimelineWidget::Hit TimelineWidget::hitTest(QPoint pos) const
{
    Hit hit;
    if (pos.x() < plotLeft() || pos.y() < rulerHeight)
        return hit;
    int top = rulerHeight;
    for (size_t i = 0; i < m_model.lanes().size(); ++i)
    {
        auto& lane = m_model.lanes()[i];
        auto h = laneHeight(lane);
        if (pos.y() >= top + h)
        {
            top += h;
            continue;
        }
        hit.lane = &lane;

        // events: anywhere on their stem, within 4 pixels
        auto from = nsAt(pos.x() - 4);
        auto to = nsAt(pos.x() + 4);
        auto it = std::partition_point(lane.events.begin(), lane.events.end(), [&](const dv::TimelineEvent& e) { return e.ns < from; });
        if (it != lane.events.end() && it->ns <= to)
        {
            hit.event = &*it;
            return hit;
        }

        // spans: in the row under the mouse, at least 3 pixels wide
        auto row = (pos.y() - top - markerArea) / rowHeight;
        if (pos.y() < top + markerArea || row >= static_cast<int>(lane.depthCount()))
            return hit;
        from = nsAt(pos.x() - 1.5);
        to = nsAt(pos.x() + 1.5);
        auto matches = [&](const dv::TimelineSpan& s) { return static_cast<int>(s.depth) == row && s.beginNs <= to && spanEnd(s) >= from; };
        auto first = std::partition_point(lane.spans.begin(), lane.spans.end(), [&](const dv::TimelineSpan& s) { return s.beginNs < from - lane.maxDuration; });
        for (auto s = first; s != lane.spans.end() && s->beginNs <= to; ++s)
            if (matches(*s))
                hit.span = &*s;
        if (!hit.span)
        {
            for (auto [l, s] : m_model.openSpans())
                if (&m_model.lanes()[l] == &lane && matches(lane.spans[s]))
                    hit.span = &lane.spans[s];
        }
        return hit;
    }
    return hit;
}

QString TimelineWidget::toolTip(const Hit& hit) const
{
    auto line = [this](uint64_t seq) { return lineLabel ? lineLabel(seq) : QString(); };
    auto relative = [this](int64_t ns) { return QString::number(static_cast<double>(ns - m_timeBase) / 1e9, 'f', 6) + " s"; };
    if (hit.event)
    {
        auto& rule = m_model.rules()[hit.event->rule];
        return toQString(rule.displayName()) + "\n" + toQString(hit.lane->name) + "\nat " + relative(hit.event->ns) + ", " + line(hit.event->seq);
    }
    if (!hit.span)
        return QString();
    auto& span = *hit.span;
    auto& rule = m_model.rules()[span.rule];
    QString text = toQString(rule.displayName()) + "\n" + toQString(hit.lane->name) + "\n";
    if (span.endNs < 0)
        text += "open for " + formatDuration(spanEnd(span) - span.beginNs) + ", from " + line(span.beginSeq);
    else
        text += formatDuration(span.endNs - span.beginNs) + ", " + line(span.beginSeq) + " to " + line(span.endSeq);

    // statistics of the completed spans of this rule in this lane
    int64_t count = 0;
    int64_t sum = 0;
    int64_t min = INT64_MAX;
    int64_t max = 0;
    for (auto& s : hit.lane->spans)
    {
        if (s.rule != span.rule || s.endNs < 0)
            continue;
        auto d = s.endNs - s.beginNs;
        ++count;
        sum += d;
        min = std::min(min, d);
        max = std::max(max, d);
    }
    if (count > 1)
        text += QString("\n%1 completed: min %2, avg %3, max %4").formatArg(QString::number(count)).formatArg(formatDuration(min)).formatArg(formatDuration(sum / count)).formatArg(formatDuration(max));
    return text;
}

// ---------------------------------------------------------------------------------------------------- input

bool TimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto help = static_cast<QHelpEvent*>(event);
        auto text = toolTip(hitTest(help->pos()));
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    auto steps = event->angleDelta().y() / 120.0;
    if (steps != 0)
        zoomAround(event->pos().x(), std::pow(0.8, steps));
    event->accept();
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    m_pressPos = event->pos();
    m_pressViewStart = m_viewStart;
    if (event->button() == Qt::LeftButton)
    {
        m_drag = Drag::Measure;
        m_measureFrom = m_measureTo = INT64_MIN;
        update();
    }
    else if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton)
    {
        m_drag = Drag::Pan;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == Drag::Pan)
    {
        m_viewStart = m_pressViewStart;
        panBy(event->pos().x() - m_pressPos.x());
    }
    else if (m_drag == Drag::Measure && std::abs(event->pos().x() - m_pressPos.x()) >= 3)
    {
        m_measureFrom = nsAt(m_pressPos.x());
        m_measureTo = nsAt(event->pos().x());
        update();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag == Drag::Measure && std::abs(event->pos().x() - m_pressPos.x()) < 3)
    {
        auto hit = hitTest(event->pos());
        if (lineClicked && hit.event)
            lineClicked(hit.event->seq);
        else if (lineClicked && hit.span)
            lineClicked(hit.span->beginSeq);
    }
    m_drag = Drag::None;
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    auto center = plotLeft() + plotWidth() / 2.0;
    switch (event->key())
    {
    case Qt::Key_A: panBy(plotWidth() / 5.0); break;
    case Qt::Key_D: panBy(-plotWidth() / 5.0); break;
    case Qt::Key_W: zoomAround(center, 0.8); break;
    case Qt::Key_S: zoomAround(center, 1.25); break;
    case Qt::Key_Home:
    case Qt::Key_Q:
        if (!m_model.empty())
            fitAll();
        m_live = true;
        update();
        break;
    case Qt::Key_Escape:
        m_measureFrom = m_measureTo = INT64_MIN;
        update();
        break;
    default: QWidget::keyPressEvent(event); return;
    }
}
