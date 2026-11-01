#include "dvcore/Ingest.h"

#include <algorithm>

namespace dv {

Ingest::Ingest(LineStore& store) :
    m_store(store),
    m_internalSink(store, 0)
{
}

Ingest::~Ingest()
{
    stop();
}

void Ingest::start()
{
    if (m_thread.joinable())
        return;
    m_thread = std::thread([this] { m_loop.run(); });
}

void Ingest::stop()
{
    if (!m_thread.joinable())
        return;
    m_loop.post([this] {
        for (auto& e : m_entries)
            stopEntry(*e);
        m_entries.clear();
    });
    // run the stop request before stopping the loop: post is FIFO
    m_loop.post([this] { m_loop.stop(); });
    m_thread.join();
}

void Ingest::applySources(std::vector<SourceConfig> configs)
{
    m_loop.post([this, configs = std::move(configs)]() mutable { apply(std::move(configs)); });
}

void Ingest::addSource(SourceConfig config)
{
    m_loop.post([this, config = std::move(config)]() mutable {
        std::vector<SourceConfig> configs;
        for (auto& e : m_entries)
            configs.push_back(e->config);
        configs.push_back(std::move(config));
        apply(std::move(configs));
    });
}

void Ingest::removeSource(const std::string& id)
{
    m_loop.post([this, id] {
        std::vector<SourceConfig> configs;
        for (auto& e : m_entries)
            if (e->config.id != id)
                configs.push_back(e->config);
        apply(std::move(configs));
    });
}

void Ingest::setPaused(bool paused)
{
    {
        std::lock_guard lock(m_mutex);
        m_paused = paused;
    }
    m_loop.post([this, paused] {
        for (auto& e : m_entries)
            if (e->source)
                e->source->setPaused(paused);
        m_internalSink.internal(paused ? "<paused>" : "<resumed>");
    });
}

bool Ingest::paused() const
{
    std::lock_guard lock(m_mutex);
    return m_paused;
}

std::vector<SourceInfo> Ingest::sources() const
{
    std::lock_guard lock(m_mutex);
    return m_snapshot;
}

void Ingest::setStatusCallback(std::function<void()> callback)
{
    std::lock_guard lock(m_mutex);
    m_statusCallback = std::move(callback);
}

void Ingest::internalMessage(std::string message)
{
    m_loop.post([this, message = std::move(message)] { m_internalSink.internal(message); });
}

uint32_t Ingest::numericId(const std::string& id)
{
    auto it = m_numericIds.find(id);
    if (it != m_numericIds.end())
        return it->second;
    auto value = m_nextNumericId++;
    m_numericIds.emplace(id, value);
    return value;
}

void Ingest::apply(std::vector<SourceConfig> configs)
{
    std::vector<std::unique_ptr<Entry>> next;
    for (auto& config : configs)
    {
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const std::unique_ptr<Entry>& e) { return e && e->config.id == config.id; });
        if (it != m_entries.end() && (*it)->config.sameRuntimeConfig(config))
        {
            (*it)->config = config; // e.g. a rename, no restart needed
            next.push_back(std::move(*it));
            continue;
        }
        if (it != m_entries.end())
        {
            stopEntry(**it);
            it->reset();
        }
        auto entry = std::make_unique<Entry>();
        entry->config = config;
        entry->available = SourceRegistry::instance().isAvailable(config.type);
        startEntry(*entry);
        next.push_back(std::move(entry));
    }
    for (auto& e : m_entries)
    {
        if (e)
            stopEntry(*e);
    }
    m_entries = std::move(next);
    notify();
}

void Ingest::startEntry(Entry& entry)
{
    if (!entry.available)
    {
        entry.status = SourceStatus{SourceState::Stopped, "not available on this platform"};
        return;
    }
    if (!entry.config.enabled)
    {
        entry.status = SourceStatus{SourceState::Stopped, "disabled"};
        return;
    }
    std::string error;
    entry.source = SourceRegistry::instance().create(entry.config, error);
    if (!entry.source)
    {
        entry.status = SourceStatus{SourceState::Error, "error: " + error};
        m_internalSink.internal(describeSource(entry.config) + ": " + error);
        return;
    }
    entry.sink = std::make_unique<Sink>(m_store, numericId(entry.config.id));
    auto* e = &entry;
    entry.source->m_statusChanged = [this, e](const SourceStatus& status) {
        e->status = status;
        notify();
    };
    entry.source->start(m_loop, *entry.sink);
    entry.status = entry.source->status();
    if (paused())
        entry.source->setPaused(true);
}

void Ingest::stopEntry(Entry& entry)
{
    if (!entry.source)
        return;
    entry.source->m_statusChanged = nullptr;
    entry.source->stop(m_loop);
    // destroy after callbacks that were already posted for this source have run (post is FIFO)
    std::shared_ptr<Source> source(std::move(entry.source));
    std::shared_ptr<Sink> sink(std::move(entry.sink));
    m_loop.post([source, sink] {});
}

void Ingest::notify()
{
    std::function<void()> callback;
    {
        std::lock_guard lock(m_mutex);
        m_snapshot.clear();
        for (auto& e : m_entries)
        {
            if (e)
                m_snapshot.push_back(SourceInfo{e->config, e->status, e->available});
        }
        callback = m_statusCallback;
    }
    if (callback)
        callback();
}

} // namespace dv
