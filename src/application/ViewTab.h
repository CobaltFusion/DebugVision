#pragma once

#include "LogViewWidget.h"

#include "dvcore/Timeline.h"

#include <QSplitter>

#include <string>
#include <vector>

class QScrollArea;
class TimelineWidget;

// The contents of a view tab: the log list, with the timeline of the view's (filtered) lines below it.
// The timeline model is only built while the timeline is shown.
class ViewTab : public QSplitter
{
    CS_OBJECT(ViewTab)

public:
    ViewTab(LogViewWidget* logView, ViewContext& context, const dv::ViewSettings& settings);

    LogViewWidget* logView() const { return m_logView; }

    // the tab that holds a log view (nullptr before it is added to one)
    static ViewTab* of(LogViewWidget* logView) { return static_cast<ViewTab*>(logView->parentWidget()); }

    // the settings of the log view and the timeline
    dv::ViewSettings settings() const;

    // called on the GUI update tick, after LogViewWidget::refresh()
    void refresh();
    // the focused line of the list changed
    void focusChanged();

    bool timelineVisible() const;
    void setTimelineVisible(bool visible);
    const std::vector<dv::TimelineRule>& timelineRules() const { return m_rules; }
    void setTimelineRules(const std::vector<dv::TimelineRule>& rules); // throws dv::TimelineError

private:
    void feed();
    const std::string& processName(uint32_t originId);

    ViewContext& m_context;
    LogViewWidget* m_logView;
    std::vector<dv::TimelineRule> m_rules;
    dv::TimelineModel m_model; // before m_timeline, which refers to it
    QScrollArea* m_scroll;
    TimelineWidget* m_timeline;
    uint64_t m_generation = UINT64_MAX; // of the view, when the model was built
    size_t m_fed = 0;                   // view lines added to the model
    int m_timelineHeight;
    std::vector<const std::string*> m_processNames; // by origin id
};
