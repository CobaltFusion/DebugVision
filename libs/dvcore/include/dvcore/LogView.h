#pragma once

#include "dvcore/FilterEngine.h"
#include "dvcore/LineStore.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace dv {

struct ViewLine
{
    uint64_t seq;
    uint16_t style;
};

// Things that happened while filtering new lines, for the GUI to act on
struct ViewEvents
{
    std::vector<uint64_t> stops;     // Stop filter matched: stop auto-scrolling, center this line
    std::vector<uint64_t> tracks;    // Track filter matched: center this line
    std::vector<uint64_t> bookmarks; // Bookmark filter matched
    int beeps = 0;
    bool cleared = false; // a Clear filter matched, or the log was cleared
    bool changed = false; // new visible lines or a re-filter
};

// The filtered result of one view: the sequence numbers of the visible lines and their row styles.
// Filtering runs on the FilterWorker thread; the GUI reads the result. All public methods are thread-safe.
class View
{
public:
    View(LineStore& store, const FilterSet& filters);

    // compiles the filters (throws FilterError) and re-filters the view in the background
    void setFilters(const FilterSet& filters);
    FilterSet filters() const;

    size_t size() const;
    // copies the visible lines [first, first + count)
    void lines(size_t first, size_t count, std::vector<ViewLine>& out) const;
    uint64_t seqAt(size_t index) const;
    // index of the first visible line with seq >= 'seq' (== size() when none)
    size_t lowerBound(uint64_t seq) const;
    RowStyle style(uint16_t style) const;
    std::vector<HighlightSpan> highlights(std::string_view message);

    void clearView();                    // hide everything that is currently in the log
    void resetView(uint64_t fromSeq = 0); // show (and re-filter) everything from 'fromSeq'
    uint64_t firstSeq() const;

    ViewEvents takeEvents();
    bool isFiltering() const { return m_filtering.load(); }
    // incremented when the visible lines were replaced (re-filter, clear); selections must be re-mapped by seq
    uint64_t generation() const { return m_generation.load(); }

private:
    friend class FilterWorker;

    struct OriginCache;
    // worker thread: returns true when something changed
    bool update(unsigned threads);
    void refilter(unsigned threads);

    LineStore& m_store;
    mutable std::mutex m_mutex;
    std::shared_ptr<const FilterEngine> m_engine; // guarded by m_mutex
    FilterState m_state;                          // guarded by m_mutex
    std::vector<uint64_t> m_seqs;                 // guarded by m_mutex
    std::vector<uint16_t> m_styles;               // guarded by m_mutex
    ViewEvents m_events;                          // guarded by m_mutex
    uint64_t m_firstSeq = 0;                      // guarded by m_mutex
    uint64_t m_processed = 0;                     // guarded by m_mutex
    uint64_t m_clearGeneration = 0;               // guarded by m_mutex
    bool m_needRefilter = false;                  // guarded by m_mutex
    std::atomic<bool> m_filtering{false};
    std::atomic<uint64_t> m_generation{0};
    std::vector<const std::string*> m_originNames; // worker thread only
};

// One thread that filters new lines for all views, and re-filters views after their filters changed.
// It sleeps until the LineStore signals new lines.
class FilterWorker
{
public:
    explicit FilterWorker(LineStore& store);
    ~FilterWorker();

    void start();
    void stop();

    std::shared_ptr<View> addView(const FilterSet& filters);
    void removeView(const std::shared_ptr<View>& view);
    void wake();

    // called (from the worker thread) when views changed; only once until acknowledge() is called
    void setNotify(std::function<void()> notify);
    void acknowledge();

private:
    void run();

    LineStore& m_store;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_wake = false;
    bool m_stop = false;
    std::vector<std::shared_ptr<View>> m_views; // guarded by m_mutex
    std::function<void()> m_notify;
    std::atomic<bool> m_notifyPending{false};
    unsigned m_threads;
};

} // namespace dv
