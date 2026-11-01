// OutputDebugString capture through the DBWIN shared memory protocol (Windows only)

#include "Sources.h"

#include "dvcore/Clock.h"

#include <windows.h>

#include <cstring>
#include <unordered_map>

namespace dv {

std::string pathFileNameUtf8(const wchar_t* path, DWORD size);

namespace {

struct DbWinBuffer
{
    DWORD processId;
    char data[4096 - sizeof(DWORD)];
};

class DbWinChannel
{
public:
    // throws std::runtime_error, e.g. when another debug viewer already owns the DBWIN objects
    explicit DbWinChannel(bool global)
    {
        std::wstring prefix = global ? L"Global\\" : L"";
        // a NULL DACL allows services and other sessions to write messages
        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
        SECURITY_ATTRIBUTES sa{sizeof(sa), &sd, FALSE};

        m_bufferReady = CreateEventW(&sa, FALSE, TRUE, (prefix + L"DBWIN_BUFFER_READY").c_str());
        auto error = GetLastError();
        if (!m_bufferReady)
            fail(global, "cannot create DBWIN_BUFFER_READY");
        if (error == ERROR_ALREADY_EXISTS)
            fail(global, "another debug output viewer (e.g. DebugView) is already running");
        m_dataReady = CreateEventW(&sa, FALSE, FALSE, (prefix + L"DBWIN_DATA_READY").c_str());
        if (!m_dataReady)
            fail(global, "cannot create DBWIN_DATA_READY");
        m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(DbWinBuffer), (prefix + L"DBWIN_BUFFER").c_str());
        if (!m_mapping)
            fail(global, "cannot create DBWIN_BUFFER");
        m_view = static_cast<const DbWinBuffer*>(MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, sizeof(DbWinBuffer)));
        if (!m_view)
            fail(global, "cannot map DBWIN_BUFFER");
    }

    ~DbWinChannel() { close(); }

    HANDLE dataReady() const { return m_dataReady; }

    // copies the message out of the shared buffer and releases the buffer to the next writer immediately
    void take(DWORD& pid, std::string& text)
    {
        pid = m_view->processId;
        auto len = strnlen(m_view->data, sizeof(m_view->data));
        text.assign(m_view->data, len);
        SetEvent(m_bufferReady);
    }

private:
    [[noreturn]] void fail(bool global, const std::string& message)
    {
        auto error = GetLastError();
        close();
        throw std::runtime_error(std::string(global ? "global " : "") + "OutputDebugString capture: " + message + " (error " + std::to_string(error) + ")");
    }

    void close()
    {
        if (m_view)
            UnmapViewOfFile(m_view);
        if (m_mapping)
            CloseHandle(m_mapping);
        if (m_dataReady)
            CloseHandle(m_dataReady);
        if (m_bufferReady)
            CloseHandle(m_bufferReady);
        m_view = nullptr;
        m_mapping = m_dataReady = m_bufferReady = nullptr;
    }

    HANDLE m_bufferReady = nullptr;
    HANDLE m_dataReady = nullptr;
    HANDLE m_mapping = nullptr;
    const DbWinBuffer* m_view = nullptr;
};

// pid -> process name, resolved once per process; the process handle is watched so the entry is dropped when the process exits
class ProcessCache
{
public:
    struct Entry
    {
        HANDLE handle = nullptr;
        uint32_t origin = 0;
        platform::EventLoop::Id watch = 0;
    };

    ~ProcessCache() { clear(); }

    void attach(platform::EventLoop& loop, Sink& sink, std::function<void(uint32_t)> onExit)
    {
        m_loop = &loop;
        m_sink = &sink;
        m_onExit = std::move(onExit);
    }

    uint32_t originFor(uint32_t pid)
    {
        auto it = m_entries.find(pid);
        if (it != m_entries.end())
            return it->second.origin;

        Entry entry;
        std::string name;
        if (pid < 5)
            name = "SYSTEM_PID_" + std::to_string(pid);
        entry.handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (entry.handle && name.empty())
        {
            wchar_t buffer[MAX_PATH * 2];
            DWORD size = static_cast<DWORD>(std::size(buffer));
            if (QueryFullProcessImageNameW(entry.handle, 0, buffer, &size))
                name = pathFileNameUtf8(buffer, size);
        }
        if (name.empty())
            name = "<unknown>";
        entry.origin = m_sink->origin(name);
        if (entry.handle && m_entries.size() < 4096)
        {
            entry.watch = m_loop->watchProcess(platform::ProcessRef{entry.handle}, [this, pid] { onProcessExit(pid); });
            m_entries.emplace(pid, entry);
        }
        else if (entry.handle)
        {
            CloseHandle(entry.handle);
        }
        return entry.origin;
    }

    void clear()
    {
        for (auto& [pid, e] : m_entries)
        {
            if (e.watch && m_loop)
                m_loop->unwatch(e.watch);
            CloseHandle(e.handle);
        }
        m_entries.clear();
    }

private:
    void onProcessExit(uint32_t pid)
    {
        auto it = m_entries.find(pid);
        if (it == m_entries.end())
            return;
        CloseHandle(it->second.handle);
        m_entries.erase(it);
        if (m_onExit)
            m_onExit(pid);
    }

    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    std::function<void(uint32_t)> m_onExit;
    std::unordered_map<uint32_t, Entry> m_entries;
};

class OutputDebugStringSource : public Source
{
public:
    explicit OutputDebugStringSource(const SourceConfig& config) :
        m_config(config),
        m_params(config.getParams<OutputDebugStringParams>()),
        m_assembler(config.encodingValue(), legacyCodecFor(config), config.autoNewline)
    {
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_cache.attach(loop, sink, [this](uint32_t pid) { m_assembler.flush(*m_sink, pid); });
        std::vector<std::string> errors;
        if (m_params.scope != "global")
            open(false, errors);
        if (m_params.scope != "local")
            open(true, errors);
        for (auto& e : errors)
            sink.internal(e);
        if (m_channels.empty())
        {
            setStatus(SourceState::Error, errors.empty() ? "error" : "error: " + errors.front());
            return;
        }
        std::string text = m_channels.size() == 2 ? "capturing local + global" : m_params.scope == "global" ? "capturing global" : "capturing local";
        setStatus(SourceState::Running, text + (errors.empty() ? std::string() : " (" + errors.front() + ")"));
    }

    void stop(platform::EventLoop& loop) override
    {
        for (auto id : m_watches)
            loop.unwatch(id);
        m_watches.clear();
        m_channels.clear();
        m_cache.clear();
        if (m_sink)
            m_assembler.flushAll(*m_sink);
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

private:
    void open(bool global, std::vector<std::string>& errors)
    {
        try
        {
            auto channel = std::make_unique<DbWinChannel>(global);
            auto raw = channel.get();
            m_watches.push_back(m_loop->watch(raw->dataReady(), [this, raw] { onData(*raw); }));
            m_channels.push_back(std::move(channel));
        }
        catch (const std::exception& e)
        {
            errors.push_back(e.what());
        }
    }

    void onData(DbWinChannel& channel)
    {
        DWORD pid = 0;
        channel.take(pid, m_text);
        if (m_paused)
            return;
        auto steady = nowSteadyNs();
        auto system = nowSystemNs();
        auto origin = m_config.processNameOverride.empty() ? m_cache.originFor(pid) : m_sink->origin(m_config.processNameOverride);
        m_assembler.add(*m_sink, steady, system, pid, origin, m_text);
    }

    SourceConfig m_config;
    OutputDebugStringParams m_params;
    MessageAssembler m_assembler;
    ProcessCache m_cache;
    std::vector<std::unique_ptr<DbWinChannel>> m_channels;
    std::vector<platform::EventLoop::Id> m_watches;
    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    std::string m_text;
    bool m_paused = false;
};

} // namespace

std::string pathFileNameUtf8(const wchar_t* path, DWORD size)
{
    return platform::pathToUtf8(std::filesystem::path(std::wstring_view(path, size)).filename());
}

std::unique_ptr<Source> createOutputDebugStringSource(const SourceConfig& config)
{
    return std::make_unique<OutputDebugStringSource>(config);
}

} // namespace dv
