#include "dvcore/LineStore.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dv {

OriginTable::OriginTable()
{
    // origin 0 is the internal source
    intern(0, "[internal]");
}

uint32_t OriginTable::intern(uint32_t sourceId, std::string_view processName)
{
    std::string key;
    key.reserve(processName.size() + 5);
    key.append(reinterpret_cast<const char*>(&sourceId), sizeof(sourceId));
    key.append(processName);

    std::lock_guard lock(m_mutex);
    auto it = m_index.find(key);
    if (it != m_index.end())
        return it->second;
    auto id = static_cast<uint32_t>(m_origins.size());
    m_origins.push_back(Origin{sourceId, std::string(processName)});
    m_index.emplace(std::move(key), id);
    return id;
}

const Origin& OriginTable::get(uint32_t originId) const
{
    std::lock_guard lock(m_mutex);
    if (originId >= m_origins.size())
        return m_origins.front();
    return m_origins[originId];
}

uint32_t OriginTable::size() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<uint32_t>(m_origins.size());
}

Chunk::Chunk(uint64_t firstSeq, uint32_t textCapacity) :
    m_firstSeq(firstSeq),
    m_textCapacity(textCapacity),
    m_records(std::make_unique<LineRecord[]>(maxLines)),
    m_text(std::make_unique<char[]>(textCapacity))
{
}

bool Chunk::fits(size_t textLen) const
{
    return m_count.load(std::memory_order_relaxed) < maxLines && m_textUsed + textLen <= m_textCapacity;
}

std::string_view Chunk::text(uint32_t index) const
{
    return text(m_records[index]);
}

void Chunk::append(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view text)
{
    auto index = m_count.load(std::memory_order_relaxed);
    auto len = static_cast<uint32_t>(text.size());
    if (len > 0)
        std::memcpy(m_text.get() + m_textUsed, text.data(), len);
    m_records[index] = LineRecord{steadyNs, systemNs, pid, originId, m_textUsed, len};
    m_textUsed += len;
    m_count.store(index + 1, std::memory_order_release);
}

LineStore::LineStore(uint64_t historyLimit) :
    m_historyLimit(historyLimit)
{
}

void LineStore::append(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view text)
{
    // lines longer than 16MB are truncated, the arena offsets are 32 bit
    constexpr size_t maxLineLength = 16 * 1024 * 1024;
    if (text.size() > maxLineLength)
        text = text.substr(0, maxLineLength);

    if (!m_current || !m_current->fits(text.size()))
    {
        auto capacity = static_cast<uint32_t>(std::max<size_t>(Chunk::defaultTextCapacity, text.size()));
        auto chunk = std::make_shared<Chunk>(m_end.load(std::memory_order_relaxed), capacity);
        std::unique_lock lock(m_mutex);
        m_chunks.push_back(chunk);
        m_current = std::move(chunk);
        enforceHistoryLimit();
    }
    m_current->append(steadyNs, systemNs, pid, originId, text);
    auto end = m_end.fetch_add(1, std::memory_order_release) + 1;
    if (end >= m_dropThreshold.load(std::memory_order_relaxed))
    {
        std::unique_lock lock(m_mutex);
        enforceHistoryLimit();
    }
    signal();
}

void LineStore::enforceHistoryLimit()
{
    auto limit = m_historyLimit.load(std::memory_order_relaxed);
    m_dropThreshold.store(UINT64_MAX, std::memory_order_relaxed);
    if (limit == 0)
        return;
    // drop whole chunks while the remaining chunks still hold at least 'limit' lines
    auto end = m_end.load(std::memory_order_relaxed);
    while (m_chunks.size() > 1 && end - m_chunks[1]->firstSeq() >= limit)
    {
        m_chunks.pop_front();
        auto first = m_chunks.front()->firstSeq();
        if (m_begin.load(std::memory_order_relaxed) < first)
            m_begin.store(first, std::memory_order_release);
    }
    if (m_chunks.size() > 1)
        m_dropThreshold.store(m_chunks[1]->firstSeq() + limit, std::memory_order_relaxed);
}

void LineStore::clear()
{
    std::unique_lock lock(m_mutex);
    m_begin.store(m_end.load(std::memory_order_acquire), std::memory_order_release);
    // keep the chunk that is being written into, it may still receive lines
    while (m_chunks.size() > 1)
        m_chunks.pop_front();
    m_clearGeneration.fetch_add(1, std::memory_order_acq_rel);
    lock.unlock();
    signal();
}

void LineStore::crop(uint64_t beginSeq)
{
    std::unique_lock lock(m_mutex);
    beginSeq = std::min(beginSeq, m_end.load(std::memory_order_acquire));
    if (beginSeq <= m_begin.load(std::memory_order_relaxed))
        return;
    m_begin.store(beginSeq, std::memory_order_release);
    while (m_chunks.size() > 1 && m_chunks[1]->firstSeq() <= beginSeq)
        m_chunks.pop_front();
    m_clearGeneration.fetch_add(1, std::memory_order_acq_rel);
    lock.unlock();
    signal();
}

void LineStore::setHistoryLimit(uint64_t lines)
{
    m_historyLimit.store(lines, std::memory_order_relaxed);
    std::unique_lock lock(m_mutex);
    enforceHistoryLimit();
}

uint64_t LineStore::historyLimit() const
{
    return m_historyLimit.load(std::memory_order_relaxed);
}

ChunkPtr LineStore::chunkFor(uint64_t seq) const
{
    std::shared_lock lock(m_mutex);
    if (m_chunks.empty() || seq < m_begin.load(std::memory_order_relaxed))
        return {};
    // chunks are ordered by firstSeq; find the last chunk with firstSeq <= seq
    auto it = std::upper_bound(m_chunks.begin(), m_chunks.end(), seq, [](uint64_t s, const std::shared_ptr<Chunk>& c) { return s < c->firstSeq(); });
    if (it == m_chunks.begin())
        return {};
    --it;
    if (seq >= (*it)->firstSeq() + (*it)->count())
        return {};
    return *it;
}

bool LineStore::get(uint64_t seq, LineRecord& record, std::string& text) const
{
    auto chunk = chunkFor(seq);
    if (!chunk)
        return false;
    auto index = static_cast<uint32_t>(seq - chunk->firstSeq());
    record = chunk->record(index);
    text.assign(chunk->text(record));
    return true;
}

} // namespace dv
