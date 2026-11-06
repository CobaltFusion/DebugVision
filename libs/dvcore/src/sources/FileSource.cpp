// File tailing: event driven (directory change notification) + an adaptive poll as safety net.
// Handles truncation (re-read from the start), rotation (rename + recreate) and files that do not exist yet.

#include "Sources.h"

#include "LogTextReader.h"

#include <algorithm>
#include <vector>

namespace dv {

namespace {

using namespace std::chrono_literals;

constexpr auto fastPoll = 50ms;
constexpr auto slowPoll = 1000ms;
constexpr auto rotationCheckInterval = 1000ms;
constexpr size_t readBlockSize = 256 * 1024;
constexpr size_t readBudget = 4 * 1024 * 1024; // per event loop turn, so one big file does not starve other sources
constexpr size_t sampleSize = 64 * 1024;

class FileSource : public Source
{
public:
    explicit FileSource(const SourceConfig& config) :
        m_config(config),
        m_params(config.getParams<FileParams>()),
        m_path(platform::utf8ToPath(m_params.path)),
        m_reader(config.encodingValue(), legacyCodecFor(config), m_params.format),
        m_buffer(readBlockSize)
    {
        m_name = config.processNameOverride.empty() ? platform::pathToUtf8(m_path.filename()) : config.processNameOverride;
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_reader.attach(sink, sink.origin(m_name));
        if (m_params.tail)
            m_watch = loop.watchFile(m_path, [this] { onChange(); });
        if (!openFile(m_params.startAt == "end"))
        {
            setStatus(SourceState::Waiting, "waiting for " + platform::pathToUtf8(m_path));
            if (!m_params.tail)
            {
                setStatus(SourceState::Error, "cannot open " + platform::pathToUtf8(m_path) + ": " + m_openError);
                sink.internal("Unable to open '" + platform::pathToUtf8(m_path) + "': " + m_openError);
                return;
            }
        }
        schedulePoll(fastPoll);
        readAvailable();
    }

    void stop(platform::EventLoop& loop) override
    {
        if (m_watch)
            loop.unwatch(m_watch);
        if (m_timer)
            loop.cancelTimer(m_timer);
        m_watch = 0;
        m_timer = 0;
        m_stopped = true;
        if (m_sink)
            flushPartial();
        m_file.close();
        setStatus(SourceState::Stopped, "stopped");
    }

private:
    bool openFile(bool atEnd)
    {
        auto file = platform::File::open(m_path, m_openError);
        if (!file)
            return false;
        m_file = std::move(*file);
        m_fileId = m_file.id();
        m_offset = 0;

        // detect the encoding and the log format from the start of the file
        std::string sample(sampleSize, '\0');
        auto n = m_file.read(sample.data(), sample.size());
        sample.resize(n > 0 ? static_cast<size_t>(n) : 0);
        m_offset = m_reader.begin(sample, platform::pathToUtf8(m_path.extension()));

        auto size = m_file.size();
        if (atEnd && size > 0)
            m_offset = static_cast<uint64_t>(size);
        m_file.seek(m_offset);
        m_lastActivity = std::chrono::steady_clock::now();
        m_nextRotationCheck = m_lastActivity + rotationCheckInterval;

        auto description = std::string(m_params.tail ? "tailing " : "loading ") + platform::pathToUtf8(m_path.filename()) + " (" + m_reader.description() + ")";
        setStatus(SourceState::Running, description);
        m_sink->internal("Started " + description);
        return true;
    }

    void onChange()
    {
        if (m_stopped)
            return;
        if (!m_file.isOpen())
        {
            if (!openFile(false))
                return;
        }
        readAvailable();
    }

    // reads at most readBudget bytes, then continues on the next event loop turn
    void readAvailable()
    {
        if (m_stopped || !m_file.isOpen() || m_readScheduled)
            return;
        auto size = m_file.size();
        if (size >= 0 && static_cast<uint64_t>(size) < m_offset)
        {
            m_sink->internal("File '" + platform::pathToUtf8(m_path) + "' was truncated, reading it from the start");
            flushPartial();
            m_file.seek(0);
            m_offset = 0;
            m_reader.reset();
        }
        size_t total = 0;
        bool eof = false;
        while (total < readBudget)
        {
            auto n = m_file.read(m_buffer.data(), m_buffer.size());
            if (n <= 0)
            {
                eof = true;
                break;
            }
            m_offset += static_cast<uint64_t>(n);
            total += static_cast<size_t>(n);
            m_reader.feed(std::string_view(m_buffer.data(), static_cast<size_t>(n)));
        }
        if (total > 0)
            m_lastActivity = std::chrono::steady_clock::now();
        if (total >= readBudget)
        {
            m_readScheduled = true;
            m_loop->post([this] {
                m_readScheduled = false;
                readAvailable();
            });
            return;
        }
        if (!m_params.tail && eof)
        {
            flushPartial();
            finishLoad();
        }
    }

    void finishLoad()
    {
        if (m_timer)
            m_loop->cancelTimer(m_timer);
        m_timer = 0;
        m_file.close();
        setStatus(SourceState::Stopped, "loaded " + platform::pathToUtf8(m_path.filename()));
    }

    void flushPartial()
    {
        m_reader.flush();
    }

    void schedulePoll(std::chrono::milliseconds interval)
    {
        m_pollInterval = interval;
        m_timer = m_loop->addTimer(std::chrono::steady_clock::now() + interval, [this] { onPoll(); });
    }

    void onPoll()
    {
        m_timer = 0;
        if (m_stopped)
            return;
        auto now = std::chrono::steady_clock::now();
        if (!m_file.isOpen())
        {
            if (openFile(false))
                readAvailable();
        }
        else
        {
            readAvailable();
            if (now >= m_nextRotationCheck)
            {
                m_nextRotationCheck = now + rotationCheckInterval;
                checkRotation();
            }
        }
        if (!m_params.tail && !m_file.isOpen())
            return;
        // adaptive: poll fast right after activity, back off to once per second when idle
        auto idle = now - m_lastActivity;
        auto next = idle < 1s ? fastPoll : std::min<std::chrono::milliseconds>(slowPoll, std::max(fastPoll, m_pollInterval * 2));
        schedulePoll(next);
    }

    void checkRotation()
    {
        auto pathId = platform::File::idOf(m_path);
        if (pathId && m_fileId && *pathId == *m_fileId)
            return;
        // the file was renamed/deleted (and maybe recreated): drain what is left in the old file, then reopen
        readAvailable();
        flushPartial();
        m_file.close();
        if (pathId)
        {
            m_sink->internal("File '" + platform::pathToUtf8(m_path) + "' was replaced, reading the new file from the start");
            openFile(false);
            if (m_watch)
            {
                // re-register, macOS watches the vnode of the old file
                m_loop->unwatch(m_watch);
                m_watch = m_loop->watchFile(m_path, [this] { onChange(); });
            }
        }
        else
        {
            setStatus(SourceState::Waiting, "waiting for " + platform::pathToUtf8(m_path));
        }
    }

    SourceConfig m_config;
    FileParams m_params;
    std::filesystem::path m_path;
    std::string m_name;
    LogTextReader m_reader;

    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    platform::File m_file;
    std::optional<platform::FileId> m_fileId;
    std::string m_openError;
    uint64_t m_offset = 0;
    std::vector<char> m_buffer;
    platform::EventLoop::Id m_watch = 0;
    platform::EventLoop::Id m_timer = 0;
    std::chrono::milliseconds m_pollInterval = fastPoll;
    std::chrono::steady_clock::time_point m_lastActivity;
    std::chrono::steady_clock::time_point m_nextRotationCheck;
    bool m_readScheduled = false;
    bool m_stopped = false;
};

} // namespace

std::unique_ptr<Source> createFileSource(const SourceConfig& config)
{
    return std::make_unique<FileSource>(config);
}

} // namespace dv
