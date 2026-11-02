#pragma once

#include "dvcore/LineStore.h"

#include <QWidget>

#include <chrono>
#include <deque>

class QTimer;

// Shows how many lines arrive: a light that is on while lines arrive, and a graph of the lines per 100 ms
// over the last 6 seconds (the traffic indication of timelens). It samples the store only while it is visible.
class TrafficIndicator : public QWidget
{
public:
    TrafficIndicator(const dv::LineStore& store, QWidget* parent);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void sample();

    const dv::LineStore& m_store;
    QTimer* m_timer = nullptr;
    uint64_t m_lastSeq = 0;
    std::chrono::steady_clock::time_point m_lastSample;
    double m_level = 0; // 0..1, smoothed
    bool m_active = false;
    std::deque<double> m_history;
};
