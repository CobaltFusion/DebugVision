#pragma once

#include "dvcore/Encoding.h"
#include "dvcore/LineStore.h"
#include "dvcore/Platform.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace dv {

enum class SourceState
{
    Stopped,
    Starting,
    Running,
    Waiting, // e.g. waiting for a file to appear, or for a reconnect
    Error,
};

const char* toText(SourceState state);

// Where sources write their lines. Owned by the ingest thread; all methods must be called on the ingest thread.
class Sink
{
public:
    Sink(LineStore& store, uint32_t sourceId);

    uint32_t sourceId() const { return m_sourceId; }
    LineStore& store() { return m_store; }

    uint32_t origin(std::string_view processName);
    // adds one complete UTF-8 line; handles the DBGVIEWCLEAR command
    void add(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view utf8);
    void add(uint32_t pid, uint32_t originId, std::string_view utf8);
    // a status message from this source, shown with process name "[internal]"
    void internal(std::string_view message);

private:
    LineStore& m_store;
    uint32_t m_sourceId;
    uint32_t m_internalOrigin;
    std::string m_lastName;
    uint32_t m_lastOrigin = 0;
};

// Assembles messages into lines per (pid, process name), for message oriented sources such as OutputDebugString and UDP.
// With autoNewline, every message is a line of its own (a trailing newline is optional).
// Without autoNewline, text without a trailing newline is kept until the newline arrives, or until 8KB is buffered.
class MessageAssembler
{
public:
    MessageAssembler(Encoding encoding, std::shared_ptr<LegacyCodec> legacy, bool autoNewline);

    void setAutoNewline(bool autoNewline) { m_autoNewline = autoNewline; }
    void add(Sink& sink, int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view raw);
    void flush(Sink& sink, uint32_t pid); // e.g. when a process terminated
    void flushAll(Sink& sink);

private:
    struct Pending
    {
        int64_t steadyNs;
        int64_t systemNs;
        std::string text;
    };

    Encoding m_encoding;
    std::shared_ptr<LegacyCodec> m_legacy;
    bool m_autoNewline;
    LineDecoder m_decoder;
    std::map<std::pair<uint32_t, uint32_t>, Pending> m_pending; // (pid, originId) -> partial line
};

struct SourceStatus
{
    SourceState state = SourceState::Stopped;
    std::string text; // e.g. "listening on 0.0.0.0:2020", "tailing app.log (utf-16le)", "error: port in use"
};

class Source
{
public:
    virtual ~Source() = default;
    // register with the event loop; called on the ingest thread
    virtual void start(platform::EventLoop& loop, Sink& sink) = 0;
    // unregister from the event loop; called on the ingest thread
    virtual void stop(platform::EventLoop& loop) = 0;
    // pause/resume capturing (only meaningful for live sources)
    virtual void setPaused(bool /*paused*/) {}

    SourceStatus status() const { return m_status; }

protected:
    void setStatus(SourceState state, std::string text);

    // set by the ingest thread, called when the status changes
    friend class Ingest;
    std::function<void(const SourceStatus&)> m_statusChanged;

private:
    SourceStatus m_status;
};

} // namespace dv
