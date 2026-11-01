#include "dvcore/LogView.h"

#include <algorithm>

namespace dv {

namespace {

constexpr size_t parallelThreshold = 200'000; // lines; below this a re-filter runs on the worker thread only
constexpr uint64_t batchSize = 16384;         // lines evaluated per lock of the view (bounds GUI wait time)

// process name of a line, cached per origin id (origins never change once created)
const std::string& processNameOf(const LineStore& store, std::vector<const std::string*>& cache, uint32_t originId)
{
    if (originId >= cache.size())
        cache.resize(originId + 1, nullptr);
    if (!cache[originId])
        cache[originId] = &store.origins().get(originId).processName;
    return *cache[originId];
}

} // namespace

View::View(LineStore& store, const FilterSet& filters) :
    m_store(store),
    m_engine(std::make_shared<FilterEngine>(filters))
{
    m_state = m_engine->makeState();
    m_firstSeq = store.beginSeq();
    m_processed = m_firstSeq;
    m_clearGeneration = store.clearGeneration();
}

void View::setFilters(const FilterSet& filters)
{
    auto engine = std::make_shared<FilterEngine>(filters); // throws FilterError
    std::lock_guard lock(m_mutex);
    m_engine = std::move(engine);
    m_needRefilter = true;
}

FilterSet View::filters() const
{
    std::lock_guard lock(m_mutex);
    return m_engine->filters();
}

size_t View::size() const
{
    std::lock_guard lock(m_mutex);
    return m_seqs.size();
}

void View::lines(size_t first, size_t count, std::vector<ViewLine>& out) const
{
    out.clear();
    std::lock_guard lock(m_mutex);
    auto end = std::min(m_seqs.size(), first + count);
    for (auto i = first; i < end; ++i)
        out.push_back(ViewLine{m_seqs[i], m_styles[i]});
}

uint64_t View::seqAt(size_t index) const
{
    std::lock_guard lock(m_mutex);
    return index < m_seqs.size() ? m_seqs[index] : UINT64_MAX;
}

size_t View::lowerBound(uint64_t seq) const
{
    std::lock_guard lock(m_mutex);
    return static_cast<size_t>(std::lower_bound(m_seqs.begin(), m_seqs.end(), seq) - m_seqs.begin());
}

RowStyle View::style(uint16_t style) const
{
    std::lock_guard lock(m_mutex);
    return style < m_state.styles.size() ? m_state.styles[style] : RowStyle{};
}

std::vector<HighlightSpan> View::highlights(std::string_view message)
{
    std::lock_guard lock(m_mutex);
    return m_engine->highlights(message, m_state);
}

void View::clearView()
{
    std::lock_guard lock(m_mutex);
    m_firstSeq = m_store.endSeq();
    m_seqs.clear();
    m_styles.clear();
    m_processed = std::max(m_processed, m_firstSeq);
    m_events.cleared = true;
    ++m_generation;
}

void View::resetView(uint64_t fromSeq)
{
    std::lock_guard lock(m_mutex);
    m_firstSeq = fromSeq;
    m_needRefilter = true;
}

uint64_t View::firstSeq() const
{
    std::lock_guard lock(m_mutex);
    return m_firstSeq;
}

ViewEvents View::takeEvents()
{
    std::lock_guard lock(m_mutex);
    return std::exchange(m_events, ViewEvents{});
}

bool View::update(unsigned threads)
{
    bool refilterNeeded = false;
    bool reset = false;
    {
        std::lock_guard lock(m_mutex);
        auto clearGeneration = m_store.clearGeneration();
        if (clearGeneration != m_clearGeneration)
        {
            // the log was cleared or cropped: drop lines that are gone
            m_clearGeneration = clearGeneration;
            auto begin = m_store.beginSeq();
            auto keep = std::lower_bound(m_seqs.begin(), m_seqs.end(), begin) - m_seqs.begin();
            m_seqs.erase(m_seqs.begin(), m_seqs.begin() + keep);
            m_styles.erase(m_styles.begin(), m_styles.begin() + keep);
            m_firstSeq = std::max(m_firstSeq, begin);
            m_processed = std::max(m_processed, begin);
            m_events.cleared = m_events.cleared || m_seqs.empty();
            m_events.changed = true;
            ++m_generation;
            reset = true;
        }
        refilterNeeded = m_needRefilter;
    }
    if (refilterNeeded)
        refilter(threads);

    bool changed = refilterNeeded || reset;
    for (;;)
    {
        std::lock_guard lock(m_mutex);
        auto from = std::max(m_processed, std::max(m_firstSeq, m_store.beginSeq()));
        auto to = std::min(m_store.endSeq(), from + batchSize);
        if (from >= to || m_needRefilter)
            break;
        auto& engine = *m_engine;
        auto end = m_store.forEach(from, to, [&](const LineRef& line) {
            auto& process = processNameOf(m_store, m_originNames, line.record->originId);
            auto result = engine.evaluate(line.text, process, m_state);
            if (result.actions & ActionClear)
            {
                m_seqs.clear();
                m_styles.clear();
                m_firstSeq = line.seq;
                m_events.cleared = true;
                ++m_generation;
            }
            if (!result.included)
                return;
            m_seqs.push_back(line.seq);
            m_styles.push_back(result.style);
            if (result.actions & ActionStop)
                m_events.stops.push_back(line.seq);
            if (result.actions & ActionTrack)
                m_events.tracks.push_back(line.seq);
            if (result.actions & ActionBookmark)
                m_events.bookmarks.push_back(line.seq);
            if (result.actions & ActionBeep)
                ++m_events.beeps;
            changed = true;
        });
        m_processed = std::max(end, to);
        if (changed)
            m_events.changed = true;
    }
    return changed;
}

void View::refilter(unsigned threads)
{
    m_filtering = true;
    std::shared_ptr<const FilterEngine> engine;
    uint64_t from = 0;
    {
        std::lock_guard lock(m_mutex);
        engine = m_engine;
        m_needRefilter = false;
        from = std::max(m_firstSeq, m_store.beginSeq());
    }
    auto to = m_store.endSeq();
    auto state = engine->makeState();
    std::vector<uint64_t> seqs;
    std::vector<uint16_t> styles;
    std::vector<uint64_t> bookmarks;

    auto evaluateRange = [&](uint64_t a, uint64_t b, FilterState& st, std::vector<uint64_t>& outSeqs, std::vector<uint16_t>& outStyles, std::vector<uint64_t>& outBookmarks,
                             std::vector<const std::string*>& names) {
        m_store.forEach(a, b, [&](const LineRef& line) {
            auto& process = processNameOf(m_store, names, line.record->originId);
            auto result = engine->evaluate(line.text, process, st);
            if (!result.included)
                return;
            outSeqs.push_back(line.seq);
            outStyles.push_back(result.style);
            if (result.actions & ActionBookmark)
                outBookmarks.push_back(line.seq);
        });
    };

    if (engine->isStateful() || threads <= 1 || to - from < parallelThreshold)
    {
        evaluateRange(from, to, state, seqs, styles, bookmarks, m_originNames);
    }
    else
    {
        // stateless filters: evaluate chunks in parallel, every thread with its own match data
        struct Part
        {
            std::vector<uint64_t> seqs;
            std::vector<uint16_t> styles;
            std::vector<uint64_t> bookmarks;
            std::vector<const std::string*> names;
        };
        std::vector<Part> parts(threads);
        std::vector<std::thread> workers;
        auto span = (to - from + threads - 1) / threads;
        for (unsigned t = 0; t < threads; ++t)
        {
            auto a = from + t * span;
            auto b = std::min(to, a + span);
            if (a >= b)
                break;
            workers.emplace_back([&, t, a, b] {
                auto st = engine->makeState();
                evaluateRange(a, b, st, parts[t].seqs, parts[t].styles, parts[t].bookmarks, parts[t].names);
            });
        }
        for (auto& w : workers)
            w.join();
        for (auto& p : parts)
        {
            seqs.insert(seqs.end(), p.seqs.begin(), p.seqs.end());
            styles.insert(styles.end(), p.styles.begin(), p.styles.end());
            bookmarks.insert(bookmarks.end(), p.bookmarks.begin(), p.bookmarks.end());
        }
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_engine == engine && !m_needRefilter)
        {
            m_seqs = std::move(seqs);
            m_styles = std::move(styles);
            m_state = std::move(state);
            m_processed = to;
            m_events.bookmarks.insert(m_events.bookmarks.end(), bookmarks.begin(), bookmarks.end());
            m_events.changed = true;
            ++m_generation;
        }
    }
    m_filtering = false;
}

FilterWorker::FilterWorker(LineStore& store) :
    m_store(store),
    m_threads(std::max(1u, std::thread::hardware_concurrency()))
{
    m_store.setListener([this] { wake(); });
}

FilterWorker::~FilterWorker()
{
    stop();
    m_store.setListener(nullptr);
}

void FilterWorker::start()
{
    if (!m_thread.joinable())
        m_thread = std::thread([this] { run(); });
}

void FilterWorker::stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_one();
    if (m_thread.joinable())
        m_thread.join();
}

std::shared_ptr<View> FilterWorker::addView(const FilterSet& filters)
{
    auto view = std::make_shared<View>(m_store, filters);
    view->resetView(m_store.beginSeq());
    {
        std::lock_guard lock(m_mutex);
        m_views.push_back(view);
    }
    wake();
    return view;
}

void FilterWorker::removeView(const std::shared_ptr<View>& view)
{
    std::lock_guard lock(m_mutex);
    std::erase(m_views, view);
}

void FilterWorker::wake()
{
    {
        std::lock_guard lock(m_mutex);
        m_wake = true;
    }
    m_cv.notify_one();
}

void FilterWorker::setNotify(std::function<void()> notify)
{
    std::lock_guard lock(m_mutex);
    m_notify = std::move(notify);
}

void FilterWorker::acknowledge()
{
    m_notifyPending = false;
}

void FilterWorker::run()
{
    for (;;)
    {
        std::vector<std::shared_ptr<View>> views;
        std::function<void()> notify;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return m_wake || m_stop; });
            if (m_stop)
                return;
            m_wake = false;
            views = m_views;
            notify = m_notify;
        }
        m_store.consumeSignal();
        bool changed = false;
        for (auto& v : views)
            changed = v->update(m_threads) || changed;
        if (changed && notify && !m_notifyPending.exchange(true))
            notify();
    }
}

} // namespace dv
