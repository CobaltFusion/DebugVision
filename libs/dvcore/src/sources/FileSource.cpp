// File tailing: event driven (directory change notification) + an adaptive poll as safety net.
// Handles truncation (re-read from the start), rotation (rename + recreate) and files that do not exist yet.

#include "Sources.h"

#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"

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
        m_legacy(legacyCodecFor(config)),
        m_buffer(readBlockSize)
    {
        m_name = config.processNameOverride.empty() ? platform::pathToUtf8(m_path.filename()) : config.processNameOverride;
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_origin = sink.origin(m_name);
        m_baseSteadyNs = nowSteadyNs();
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
        auto configured = m_config.encodingValue();
        auto detected = detectEncoding(sample);
        auto encoding = configured == Encoding::Auto ? detected.encoding : configured;
        m_decoder = LineDecoder(encoding, m_legacy);
        m_offset = detected.encoding == encoding ? detected.bomLength : 0;
        m_format = detectFormat(sample, encoding);
        m_firstLine = m_format == LogFormat::DbLog;
        m_sysinternals = SysinternalsParser{};

        auto size = m_file.size();
        if (atEnd && size > 0)
            m_offset = static_cast<uint64_t>(size);
        m_file.seek(m_offset);
        m_lastActivity = std::chrono::steady_clock::now();
        m_nextRotationCheck = m_lastActivity + rotationCheckInterval;

        auto description = std::string(m_params.tail ? "tailing " : "loading ") + platform::pathToUtf8(m_path.filename()) + " (" + toText(encoding) +
            (m_format != LogFormat::Text ? std::string(", ") + toText(m_format) : std::string()) + ")";
        setStatus(SourceState::Running, description);
        m_sink->internal("Started " + description);
        return true;
    }

    LogFormat detectFormat(const std::string& sample, Encoding encoding)
    {
        if (m_params.format == "text")
            return LogFormat::Text;
        if (m_params.format == "dblog")
            return LogFormat::DbLog;
        if (m_params.format == "sysinternals")
            return LogFormat::Sysinternals;
        // decode the first two lines of the sample
        std::vector<std::string> lines;
        LineDecoder probe(encoding, m_legacy);
        probe.feed(sample, [&](std::string_view line) {
            if (lines.size() < 2)
                lines.emplace_back(line);
        });
        lines.resize(2);
        return detectLogFormat(lines[0], lines[1], platform::pathToUtf8(m_path.extension()));
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
            m_decoder.reset();
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
            m_decoder.feed(std::string_view(m_buffer.data(), static_cast<size_t>(n)), [this](std::string_view line) { addLine(line); });
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

    void addLine(std::string_view line)
    {
        if (m_format == LogFormat::Text)
        {
            m_sink->add(0, m_origin, line);
            return;
        }
        ParsedLine parsed;
        bool ok = false;
        if (m_format == LogFormat::DbLog)
        {
            if (m_firstLine)
            {
                m_firstLine = false;
                if (isDbLogHeader(line))
                    return;
            }
            ok = parseDbLogLine(line, parsed);
        }
        else
        {
            ok = m_sysinternals.parse(line, parsed);
        }
        if (!ok)
        {
            m_sink->add(0, m_origin, line);
            return;
        }
        auto now = nowSystemNs();
        auto systemNs = parsed.hasSystemTime ? parsed.systemNs : now;
        if (!m_haveFirstSystem)
        {
            m_firstSystemNs = systemNs;
            m_haveFirstSystem = true;
        }
        auto relativeNs = parsed.hasRelativeTime ? static_cast<int64_t>(parsed.relativeSeconds * 1e9) : systemNs - m_firstSystemNs;
        auto origin = parsed.processName.empty() ? m_origin : m_sink->origin(parsed.processName);
        m_sink->add(m_baseSteadyNs + relativeNs, systemNs, parsed.pid, origin, parsed.message);
    }

    void flushPartial()
    {
        m_decoder.flush([this](std::string_view line) { addLine(line); });
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
    std::shared_ptr<LegacyCodec> m_legacy;
    LineDecoder m_decoder;
    LogFormat m_format = LogFormat::Text;
    SysinternalsParser m_sysinternals;
    bool m_firstLine = false;

    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    uint32_t m_origin = 0;
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
    int64_t m_baseSteadyNs = 0;
    int64_t m_firstSystemNs = 0;
    bool m_haveFirstSystem = false;
};

} // namespace

std::unique_ptr<Source> createFileSource(const SourceConfig& config)
{
    return std::make_unique<FileSource>(config);
}

} // namespace dv
