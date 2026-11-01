#include "dvcore/Source.h"

#include "dvcore/Clock.h"

namespace dv {

namespace {

constexpr std::string_view clearCommand = "DBGVIEWCLEAR";
constexpr size_t maxPendingMessage = 8192;

} // namespace

const char* toText(SourceState state)
{
    switch (state)
    {
    case SourceState::Stopped: return "stopped";
    case SourceState::Starting: return "starting";
    case SourceState::Running: return "running";
    case SourceState::Waiting: return "waiting";
    case SourceState::Error: return "error";
    }
    return "unknown";
}

Sink::Sink(LineStore& store, uint32_t sourceId) :
    m_store(store),
    m_sourceId(sourceId),
    m_internalOrigin(store.origins().intern(sourceId, "[internal]"))
{
}

uint32_t Sink::origin(std::string_view processName)
{
    // most sources use one or a few process names; avoid the interning lock for repeats
    if (processName == m_lastName && !m_lastName.empty())
        return m_lastOrigin;
    m_lastName.assign(processName);
    m_lastOrigin = m_store.origins().intern(m_sourceId, processName);
    return m_lastOrigin;
}

void Sink::add(int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view utf8)
{
    if (utf8.size() >= clearCommand.size() && utf8.substr(0, clearCommand.size()) == clearCommand)
    {
        m_store.clear();
        return;
    }
    m_store.append(steadyNs, systemNs, pid, originId, utf8);
}

void Sink::add(uint32_t pid, uint32_t originId, std::string_view utf8)
{
    add(nowSteadyNs(), nowSystemNs(), pid, originId, utf8);
}

void Sink::internal(std::string_view message)
{
    m_store.append(nowSteadyNs(), nowSystemNs(), 0, m_internalOrigin, message);
}

MessageAssembler::MessageAssembler(Encoding encoding, std::shared_ptr<LegacyCodec> legacy, bool autoNewline) :
    m_encoding(encoding == Encoding::Auto ? Encoding::Utf8OrLegacy : encoding),
    m_legacy(std::move(legacy)),
    m_autoNewline(autoNewline),
    m_decoder(m_encoding, m_legacy)
{
}

void MessageAssembler::add(Sink& sink, int64_t steadyNs, int64_t systemNs, uint32_t pid, uint32_t originId, std::string_view raw)
{
    auto key = std::make_pair(pid, originId);
    auto it = m_pending.find(key);
    if (it == m_pending.end() && m_autoNewline && raw.find('\n') == std::string_view::npos)
    {
        // fast path: one message is one line
        if (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);
        sink.add(steadyNs, systemNs, pid, originId, m_decoder.decodeLine(raw));
        return;
    }

    std::string combined;
    int64_t lineSteady = steadyNs;
    int64_t lineSystem = systemNs;
    if (it != m_pending.end())
    {
        combined = std::move(it->second.text);
        lineSteady = it->second.steadyNs;
        lineSystem = it->second.systemNs;
        m_pending.erase(it);
    }
    combined.append(raw);

    std::string_view rest = combined;
    for (;;)
    {
        auto nl = rest.find('\n');
        if (nl == std::string_view::npos)
            break;
        auto line = rest.substr(0, nl);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        sink.add(lineSteady, lineSystem, pid, originId, m_decoder.decodeLine(line));
        rest.remove_prefix(nl + 1);
        lineSteady = steadyNs;
        lineSystem = systemNs;
    }
    if (rest.empty())
        return;
    if (m_autoNewline || rest.size() >= maxPendingMessage)
    {
        if (rest.back() == '\r')
            rest.remove_suffix(1);
        sink.add(lineSteady, lineSystem, pid, originId, m_decoder.decodeLine(rest));
        return;
    }
    m_pending.emplace(key, Pending{lineSteady, lineSystem, std::string(rest)});
}

void MessageAssembler::flush(Sink& sink, uint32_t pid)
{
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        if (it->first.first == pid)
        {
            sink.add(it->second.steadyNs, it->second.systemNs, pid, it->first.second, m_decoder.decodeLine(it->second.text));
            it = m_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void MessageAssembler::flushAll(Sink& sink)
{
    for (auto& [key, pending] : m_pending)
        sink.add(pending.steadyNs, pending.systemNs, key.first, key.second, m_decoder.decodeLine(pending.text));
    m_pending.clear();
}

void Source::setStatus(SourceState state, std::string text)
{
    m_status.state = state;
    m_status.text = std::move(text);
    if (m_statusChanged)
        m_statusChanged(m_status);
}

} // namespace dv
