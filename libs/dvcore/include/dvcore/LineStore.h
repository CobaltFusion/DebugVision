#pragma once

#include <atomic>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dv {

// One stored log line; the text lives in the chunk's UTF-8 arena.
struct LineRecord
{
    int64_t steadyNs;
    int64_t systemNs;
    uint32_t pid;
    uint32_t originId;
    uint32_t textOffset;
    uint32_t textLen;
};
static_assert(sizeof(LineRecord) == 32);

// An origin is the (source, process name) pair a line came from, interned to a small id.
struct Origin
{
    uint32_t sourceId;
    std::string processName;
};

class OriginTable
{
public:
    OriginTable();
    uint32_t intern(uint32_t sourceId, std::string_view processName);
    // the returned reference stays valid for the lifetime of the table
    const Origin& get(uint32_t originId) const;
    uint32_t size() const;

private:
    mutable std::mutex m_mutex;
    std::deque<Origin> m_origins;
    std::unordered_map<std::string, uint32_t> m_index;
};

class Chunk
{
public:
    static constexpr uint32_t maxLines = 65536;
    static constexpr uint32_t defaultTextCapacity = 4 * 1024 * 1024;

    Chunk(uint64_t firstSeq, uint32_t textCapacity);

    uint64_t firstSeq() const { return m_firstSeq; }
    uint32_t count() const { return m_count.load(std::memory_order_acquire); }
    bool fits(size_t textLen) const;

    const LineRecord& record(uint32_t index) const { return m_records[index]; }
    std::string_view text(uint32_t index) const;
    std::string_view text(const LineRecord& rec) const { return {m_text.get() + rec.textOffset, rec.textLen}; }

    // writer only
    void append(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view text);

private:
    uint64_t m_firstSeq;
    uint32_t m_textCapacity;
    uint32_t m_textUsed = 0;
    std::atomic<uint32_t> m_count{0};
    std::unique_ptr<LineRecord[]> m_records;
    std::unique_ptr<char[]> m_text;
};

using ChunkPtr = std::shared_ptr<const Chunk>;

struct LineRef
{
    uint64_t seq;
    const LineRecord* record;
    std::string_view text;
};

// Append-only storage of log lines.
// - single writer (the ingest thread), many concurrent readers
// - lines are identified by a monotonic sequence number that is never reused
// - lines are kept in chunks that never move; readers keep chunks alive with shared_ptr
// - the history limit drops whole chunks from the front
class LineStore
{
public:
    explicit LineStore(uint64_t historyLimit = 0);

    // writer API (ingest thread)
    void append(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view text);

    // thread-safe
    void clear();                  // drop all lines
    void crop(uint64_t beginSeq);  // drop all lines before beginSeq
    void setHistoryLimit(uint64_t lines); // 0 = unlimited
    uint64_t historyLimit() const;

    uint64_t beginSeq() const { return m_begin.load(std::memory_order_acquire); }
    uint64_t endSeq() const { return m_end.load(std::memory_order_acquire); }
    uint64_t size() const { return endSeq() - beginSeq(); }
    // incremented by clear(), so consumers can detect a reset
    uint64_t clearGeneration() const { return m_clearGeneration.load(std::memory_order_acquire); }

    OriginTable& origins() { return m_origins; }
    const OriginTable& origins() const { return m_origins; }

    // returns the chunk holding 'seq', or nullptr when it was dropped or not written yet
    ChunkPtr chunkFor(uint64_t seq) const;

    // calls f(const LineRef&) for every available line in [from, to); returns the seq after the last visited line
    template <typename F>
    uint64_t forEach(uint64_t from, uint64_t to, F&& f) const
    {
        from = std::max(from, beginSeq());
        to = std::min(to, endSeq());
        while (from < to)
        {
            auto chunk = chunkFor(from);
            if (!chunk)
                return from;
            auto index = static_cast<uint32_t>(from - chunk->firstSeq());
            auto count = chunk->count();
            for (; index < count && from < to; ++index, ++from)
            {
                const LineRecord& rec = chunk->record(index);
                f(LineRef{from, &rec, chunk->text(rec)});
            }
        }
        return from;
    }

    // Called (from the writing thread, or the thread calling clear/crop) when lines were appended or removed,
    // only on the transition from 'no pending signal' to 'pending'; the consumer calls consumeSignal() before reading.
    // Set it once, before the writer starts.
    void setListener(std::function<void()> listener) { m_listener = std::move(listener); }
    bool consumeSignal() { return m_signaled.exchange(false, std::memory_order_acq_rel); }

    // copy of a single line's text (convenience for non-hot paths)
    bool get(uint64_t seq, LineRecord& record, std::string& text) const;

private:
    void enforceHistoryLimit(); // requires unique lock
    void signal()
    {
        if (!m_signaled.load(std::memory_order_relaxed) && !m_signaled.exchange(true, std::memory_order_acq_rel) && m_listener)
            m_listener();
    }

    mutable std::shared_mutex m_mutex;
    std::deque<std::shared_ptr<Chunk>> m_chunks; // guarded by m_mutex
    std::shared_ptr<Chunk> m_current;            // writer only (replaced under m_mutex)
    std::atomic<uint64_t> m_begin{0};
    std::atomic<uint64_t> m_end{0};
    std::atomic<uint64_t> m_clearGeneration{0};
    std::atomic<uint64_t> m_historyLimit{0};
    std::atomic<uint64_t> m_dropThreshold{UINT64_MAX}; // the end seq at which the front chunk can be dropped
    OriginTable m_origins;
    std::atomic<bool> m_signaled{false};
    std::function<void()> m_listener;
};

} // namespace dv
