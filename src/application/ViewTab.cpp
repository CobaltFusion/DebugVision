#include "ViewTab.h"

#include "Text.h"
#include "TimelineWidget.h"

#include <QScrollArea>

#include <algorithm>

ViewTab::ViewTab(LogViewWidget* logView, ViewContext& context, const dv::ViewSettings& settings) :
    QSplitter(Qt::Vertical),
    m_context(context),
    m_logView(logView),
    m_scroll(new QScrollArea),
    m_timeline(new TimelineWidget(m_model, *context.timeBase)),
    m_timelineHeight(settings.timelineHeight)
{
    setChildrenCollapsible(false);
    addWidget(logView);
    m_scroll->setWidget(m_timeline);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setMinimumHeight(60);
    m_scroll->hide();
    addWidget(m_scroll);
    setStretchFactor(0, 1);
    setStretchFactor(1, 0);

    m_timeline->lineClicked = [this](uint64_t seq) { m_logView->setFocusSeq(seq); };
    m_timeline->lineLabel = [this](uint64_t seq) { return "line " + QString::number(m_logView->view()->lowerBound(seq) + 1); };

    try
    {
        setTimelineRules(settings.timelineRules);
    }
    catch (const dv::TimelineError&)
    {
        // an invalid rule in the settings: keep the rules for the dialog, show no timeline items
        m_rules = settings.timelineRules;
    }
    if (settings.showTimeline)
        setTimelineVisible(true);
}

dv::ViewSettings ViewTab::settings() const
{
    auto s = m_logView->settings();
    s.timelineRules = m_rules;
    s.showTimeline = timelineVisible();
    s.timelineHeight = timelineVisible() && sizes().size() == 2 ? sizes()[1] : m_timelineHeight;
    return s;
}

bool ViewTab::timelineVisible() const
{
    return !m_scroll->isHidden();
}

void ViewTab::setTimelineVisible(bool visible)
{
    if (visible == timelineVisible())
        return;
    if (visible)
    {
        m_scroll->show();
        auto total = std::max(height(), 2 * m_timelineHeight);
        setSizes({total - m_timelineHeight, m_timelineHeight});
        feed();
        focusChanged();
    }
    else
    {
        m_timelineHeight = sizes()[1];
        m_scroll->hide();
        // the model is only kept while the timeline is shown
        m_model.reset();
        m_generation = UINT64_MAX;
    }
}

void ViewTab::setTimelineRules(const std::vector<dv::TimelineRule>& rules)
{
    dv::TimelineModel model(rules); // throws for invalid rules
    m_rules = rules;
    m_model = std::move(model);
    m_generation = UINT64_MAX;
    m_timeline->rulesChanged();
    if (timelineVisible())
        feed();
}

void ViewTab::refresh()
{
    if (timelineVisible())
        feed();
}

void ViewTab::focusChanged()
{
    if (!timelineVisible())
        return;
    auto seq = m_logView->focusSeq();
    int64_t ns = INT64_MIN;
    if (seq != UINT64_MAX)
    {
        if (auto chunk = m_context.store->chunkFor(seq))
            ns = chunk->record(static_cast<uint32_t>(seq - chunk->firstSeq())).steadyNs;
    }
    m_timeline->setCursorNs(ns);
}

const std::string& ViewTab::processName(uint32_t originId)
{
    if (originId >= m_processNames.size())
        m_processNames.resize(originId + 1, nullptr);
    if (!m_processNames[originId])
        m_processNames[originId] = &m_context.store->origins().get(originId).processName;
    return *m_processNames[originId];
}

// adds the view lines that are not in the model yet; rebuilds it after a refilter, clear or crop
void ViewTab::feed()
{
    auto view = m_logView->view();
    bool rebuilt = view->generation() != m_generation || view->size() < m_fed;
    if (rebuilt)
    {
        m_generation = view->generation();
        m_model.reset();
        m_fed = 0;
    }
    std::vector<dv::ViewLine> lines;
    dv::ChunkPtr chunk;
    auto size = view->size();
    while (m_fed < size)
    {
        view->lines(m_fed, std::min<size_t>(size - m_fed, 65536), lines);
        if (lines.empty())
            break;
        for (auto& line : lines)
        {
            if (!chunk || line.seq < chunk->firstSeq() || line.seq >= chunk->firstSeq() + chunk->count())
                chunk = m_context.store->chunkFor(line.seq);
            if (!chunk)
                continue; // dropped by the history limit
            auto& record = chunk->record(static_cast<uint32_t>(line.seq - chunk->firstSeq()));
            m_model.add(line.seq, record.steadyNs, chunk->text(record), processName(record.originId), record.pid);
        }
        m_fed += lines.size();
    }
    m_timeline->modelChanged(m_logView->autoScroll(), rebuilt);
}
