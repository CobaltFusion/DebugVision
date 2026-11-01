#pragma once

#include "dvcore/LineStore.h"
#include "dvcore/Platform.h"
#include "dvcore/Source.h"
#include "dvcore/SourceConfig.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dv {

struct SourceInfo
{
    SourceConfig config;
    SourceStatus status;
    bool available = true; // false: the type does not exist on this platform (kept in the settings, never started)
};

// The ingest thread: owns the event loop and all sources, and appends their lines to the LineStore.
// All public methods are thread-safe.
class Ingest
{
public:
    explicit Ingest(LineStore& store);
    ~Ingest();

    void start();
    void stop();

    // Makes the running sources match 'configs': only added, removed or changed sources are (re)started.
    void applySources(std::vector<SourceConfig> configs);
    void addSource(SourceConfig config);
    void removeSource(const std::string& id);
    void setPaused(bool paused);
    bool paused() const;

    std::vector<SourceInfo> sources() const;
    // called on the ingest thread whenever a source status changes, or sources are added/removed
    void setStatusCallback(std::function<void()> callback);
    // posts an internal message
    void internalMessage(std::string message);

    platform::EventLoop& loop() { return m_loop; }

private:
    struct Entry
    {
        SourceConfig config;
        std::unique_ptr<Source> source;
        std::unique_ptr<Sink> sink;
        SourceStatus status;
        bool available = true;
    };

    void apply(std::vector<SourceConfig> configs); // on the loop thread
    void startEntry(Entry& entry);
    void stopEntry(Entry& entry);
    uint32_t numericId(const std::string& id);
    void notify();

    LineStore& m_store;
    platform::EventLoop m_loop;
    std::thread m_thread;
    Sink m_internalSink;

    mutable std::mutex m_mutex; // guards the snapshot fields below
    std::vector<SourceInfo> m_snapshot;
    std::function<void()> m_statusCallback;
    bool m_paused = false;

    // loop thread only
    std::vector<std::unique_ptr<Entry>> m_entries;
    std::map<std::string, uint32_t> m_numericIds;
    uint32_t m_nextNumericId = 1;
};

} // namespace dv
