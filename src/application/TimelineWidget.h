#pragma once

#include "dvcore/Timeline.h"

#include <QColor>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <vector>

// Draws a dv::TimelineModel like TimeLens: a time ruler, one lane per process with the spans as bars (stacked when
// they overlap) and the events as lollipop, square or triangle markers. Only the visible items are drawn.
//
// Mouse wheel zooms around the cursor, dragging with the right or middle button pans, dragging with the left button
// measures, a click on a bar or marker selects its line. Keys: a/d pan, w/s zoom, Home or q fit and follow live.
// While live (the default) the newest line stays at the right edge; zooming or panning stops that.
class TimelineWidget : public QWidget
{
    CS_OBJECT(TimelineWidget)

public:
    TimelineWidget(const dv::TimelineModel& model, const int64_t& timeBase, QWidget* parent = nullptr);

    // call after the model changed; 'follow' keeps the newest line in view (the list's Auto Scroll)
    void modelChanged(bool follow);
    void rulesChanged(); // recomputes the rule colors
    void setCursorNs(int64_t ns); // the time of the line focused in the list, INT64_MIN for none

    std::function<void(uint64_t seq)> lineClicked;
    std::function<QString(uint64_t seq)> lineLabel; // e.g. "line 12", for tooltips

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Hit
    {
        const dv::TimelineLane* lane = nullptr;
        const dv::TimelineSpan* span = nullptr;
        const dv::TimelineEvent* event = nullptr;
    };

    int labelWidth() const;
    int plotLeft() const { return labelWidth(); }
    int plotWidth() const;
    int laneHeight(const dv::TimelineLane& lane) const;
    int contentHeight() const;
    double xOf(int64_t ns) const;
    int64_t nsAt(double x) const;
    int64_t spanEnd(const dv::TimelineSpan& span) const;
    QColor ruleColor(uint32_t rule) const;
    Hit hitTest(QPoint pos) const;
    QString toolTip(const Hit& hit) const;
    void fitAll();
    void followEnd();
    void zoomAround(double x, double factor);
    void panBy(double dx);
    void drawRuler(QPainter& painter);
    void drawLane(QPainter& painter, const dv::TimelineLane& lane, int top);
    void drawMarker(QPainter& painter, dv::MarkerShape shape, int x, int top, int bottom, const QColor& color);

    const dv::TimelineModel& m_model;
    const int64_t& m_timeBase;
    std::vector<QColor> m_ruleColors;
    int64_t m_viewStart = 0;
    int64_t m_viewWidth = 10'000'000'000; // ns
    bool m_live = true;
    bool m_fitted = false;
    int64_t m_cursorNs = INT64_MIN;

    // mouse
    enum class Drag { None, Pan, Measure };
    Drag m_drag = Drag::None;
    QPoint m_pressPos;
    int64_t m_pressViewStart = 0;
    int64_t m_measureFrom = INT64_MIN;
    int64_t m_measureTo = INT64_MIN;
};
