// HTTP(S) tailing: like 'tail -f' for a log file on a web server. A helper thread requests the bytes after the ones it
// already has with HTTP Range requests, in chunks while there is more, then once per poll interval. The lines are decoded
// on the ingest thread, with the encoding and log format detection of the file source.
// Handles servers without Range support (the whole file is downloaded on every poll) and truncated files (the file is
// read again from the start). Network and HTTP errors are retried at every poll.

#include "Sources.h"

#include "LogTextReader.h"

#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace dv {

namespace {

constexpr uint64_t chunkSize = 1024 * 1024;

// "https://host/logs/app.log?x=1" -> "app.log"; the host when the path is empty
std::string urlFileName(const std::string& url)
{
    auto schemeEnd = url.find("://");
    auto start = schemeEnd == std::string::npos ? 0 : schemeEnd + 3;
    auto end = url.find_first_of("?#", start);
    auto path = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string urlExtension(const std::string& url)
{
    auto name = urlFileName(url);
    auto dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : name.substr(dot);
}

std::string httpStatusText(int status)
{
    switch (status)
    {
    case 401: return "HTTP 401 unauthorized";
    case 403: return "HTTP 403 forbidden";
    case 404: return "HTTP 404 not found";
    case 500: return "HTTP 500 internal server error";
    case 503: return "HTTP 503 service unavailable";
    default: return "HTTP " + std::to_string(status);
    }
}

class HttpSource : public Source
{
public:
    explicit HttpSource(const SourceConfig& config) :
        m_params(config.getParams<HttpParams>()),
        m_reader(config.encodingValue(), legacyCodecFor(config), m_params.format),
        m_shared(std::make_shared<Shared>())
    {
        m_name = config.processNameOverride.empty() ? urlFileName(m_params.url) : config.processNameOverride;
        if (m_name.empty())
            m_name = m_params.url;
        m_pollInterval = std::chrono::milliseconds(std::max(100, m_params.pollMilliseconds));
    }

    ~HttpSource() override { stopThread(); }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_reader.attach(sink, sink.origin(m_name));
        std::string error;
        m_client = platform::HttpClient::create(m_params.proxy, error);
        if (!m_client)
        {
            setStatus(SourceState::Error, error);
            sink.internal("Unable to read '" + m_params.url + "': " + error);
            return;
        }
        setStatus(SourceState::Starting, "connecting to " + m_params.url + (proxyText().empty() ? "" : " " + proxyText()));
        m_thread = std::thread([this, shared = m_shared] { run(shared); });
    }

    void stop(platform::EventLoop&) override
    {
        stopThread();
        if (m_started)
            m_reader.flush();
        setStatus(SourceState::Stopped, "stopped");
    }

private:
    // shared between the helper thread and the callbacks it posts, which can still run after the source was stopped
    struct Shared
    {
        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false; // guarded by mutex
        bool stopped = false;  // only accessed on the ingest thread: the posted callbacks must not touch the source
    };

    void stopThread()
    {
        {
            std::lock_guard lock(m_shared->mutex);
            m_shared->stopping = true;
        }
        m_shared->wake.notify_all();
        if (m_client)
            m_client->cancel();
        if (m_thread.joinable())
            m_thread.join();
        m_shared->stopped = true;
    }

    // ---------------------------------------------------------------------------------------- helper thread

    // runs 'fn' on the ingest thread, unless the source was stopped in the meantime
    template <typename Fn>
    void post(const std::shared_ptr<Shared>& shared, Fn fn)
    {
        m_loop->post([shared, fn = std::move(fn)]() mutable {
            if (!shared->stopped)
                fn();
        });
    }

    bool stopping(const std::shared_ptr<Shared>& shared)
    {
        std::lock_guard lock(shared->mutex);
        return shared->stopping;
    }

    // waits for the poll interval; returns false when the source is stopping
    bool waitPoll(const std::shared_ptr<Shared>& shared, std::chrono::milliseconds interval)
    {
        std::unique_lock lock(shared->mutex);
        return !shared->wake.wait_for(lock, interval, [&] { return shared->stopping; });
    }

    void run(const std::shared_ptr<Shared>& shared)
    {
        uint64_t offset = 0;
        bool started = false;       // the start of the file was received
        bool rangeWarned = false;
        std::string lastError;
        auto interval = m_pollInterval;

        while (!stopping(shared))
        {
            auto response = m_client->get(m_params.url, offset, offset + chunkSize - 1);
            if (stopping(shared))
                return;

            if (response.status == 0 || (response.status != 200 && response.status != 206 && response.status != 416))
            {
                auto error = response.status == 0 ? response.error : httpStatusText(response.status);
                if (error != lastError)
                {
                    lastError = error;
                    post(shared, [this, error] {
                        m_sink->internal("Reading '" + m_params.url + "' failed: " + error + (m_params.tail ? ", retrying" : ""));
                        setStatus(m_params.tail ? SourceState::Waiting : SourceState::Error, error + ": " + m_params.url);
                    });
                }
                if (!m_params.tail)
                    return;
                // back off while the server is not reachable
                interval = std::min<std::chrono::milliseconds>(std::max(interval * 2, m_pollInterval), std::chrono::seconds(10));
                if (!waitPoll(shared, interval))
                    return;
                continue;
            }
            interval = m_pollInterval;
            if (!lastError.empty())
            {
                lastError.clear();
                if (started)
                    post(shared, [this] {
                        m_sink->internal("Reading '" + m_params.url + "' again");
                        setStatus(SourceState::Running, runningText());
                    });
            }

            auto range = parseContentRange(response.contentRange);
            bool more = false;     // request the next chunk right away
            bool restart = false;  // the data starts at the beginning of the file (again)
            std::string data;
            if (response.status == 416)
            {
                // nothing after 'offset': the file did not grow, or it was truncated
                if (range.total >= 0 && static_cast<uint64_t>(range.total) < offset)
                {
                    offset = 0;
                    more = true;
                }
            }
            else if (response.status == 206)
            {
                if (range.first >= 0 && static_cast<uint64_t>(range.first) != offset)
                {
                    // not the requested range: start over
                    offset = 0;
                    more = true;
                }
                else
                {
                    restart = offset == 0;
                    data = std::move(response.body);
                    offset += data.size();
                    more = data.size() >= chunkSize || (range.total >= 0 && offset < static_cast<uint64_t>(range.total));
                }
            }
            else // 200: the server sent the whole file
            {
                if (!rangeWarned && offset > 0)
                {
                    rangeWarned = true;
                    post(shared, [this] { m_sink->internal("The server of '" + m_params.url + "' does not support range requests, downloading the whole file at every poll"); });
                }
                if (response.body.size() < offset || offset == 0)
                {
                    restart = true;
                    data = std::move(response.body);
                }
                else
                {
                    data = response.body.substr(offset);
                }
                offset = restart ? data.size() : offset + data.size();
            }

            if (restart)
            {
                bool truncated = started;
                bool skip = !started && m_params.startAt == "end"; // only use the start of the file to detect the format
                started = true;
                auto sample = data;
                post(shared, [this, sample = std::move(sample), truncated, skip] { begin(sample, truncated, skip); });
                if (skip)
                {
                    // continue at the end of the file
                    if (range.total >= 0)
                        offset = static_cast<uint64_t>(range.total);
                    data.clear();
                    more = false;
                }
            }
            if (!data.empty())
                post(shared, [this, data = std::move(data)] { feed(data); });

            if (!m_params.tail && !more)
            {
                post(shared, [this] { finishLoad(); });
                return;
            }
            if (!more && !waitPoll(shared, interval))
                return;
        }
    }

    // ---------------------------------------------------------------------------------------- ingest thread

    std::string runningText() const
    {
        return std::string(m_params.tail ? "tailing " : "loading ") + m_params.url + " (" + m_reader.description() + ")" +
            (proxyText().empty() ? "" : " " + proxyText());
    }

    // e.g. "via proxy socks5h://proxy:1080", without the password
    std::string proxyText() const
    {
        if (m_params.proxy.empty() || m_params.proxy == "system")
            return {};
        if (m_params.proxy == "direct")
            return "without proxy";
        platform::HttpProxy proxy;
        std::string error;
        if (!platform::parseHttpProxy(m_params.proxy, proxy, error))
            return {};
        auto host = proxy.host.find(':') != std::string::npos ? "[" + proxy.host + "]" : proxy.host;
        return "via proxy " + proxy.scheme + "://" + host + ":" + std::to_string(proxy.port);
    }

    // the first bytes of the file arrived; 'sample' is followed by a feed() of the same bytes unless 'skip' is set
    void begin(const std::string& sample, bool truncated, bool skip)
    {
        if (truncated)
        {
            m_sink->internal("'" + m_params.url + "' was truncated, reading it from the start");
            m_reader.flush();
        }
        m_skipBom = m_reader.begin(sample, urlExtension(m_params.url));
        if (skip)
            m_skipBom = 0;
        m_started = true;
        setStatus(SourceState::Running, runningText());
        if (!truncated)
            m_sink->internal("Started " + runningText());
    }

    void feed(std::string_view data)
    {
        if (m_skipBom > 0)
        {
            auto n = std::min(m_skipBom, data.size());
            data.remove_prefix(n);
            m_skipBom -= n;
        }
        m_reader.feed(data);
    }

    void finishLoad()
    {
        m_reader.flush();
        setStatus(SourceState::Stopped, "loaded " + m_params.url);
    }

    HttpParams m_params;
    std::string m_name;
    LogTextReader m_reader;
    std::chrono::milliseconds m_pollInterval{1000};
    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    std::unique_ptr<platform::HttpClient> m_client;
    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
    bool m_started = false;
    size_t m_skipBom = 0;
};

} // namespace

ContentRange parseContentRange(std::string_view header)
{
    ContentRange range;
    auto parse = [](std::string_view text, int64_t& value) {
        int64_t v = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
        if (ec == std::errc() && end == text.data() + text.size())
            value = v;
    };
    constexpr std::string_view unit = "bytes ";
    if (header.substr(0, unit.size()) != unit)
        return range;
    header.remove_prefix(unit.size());
    auto slash = header.find('/');
    if (slash == std::string_view::npos)
        return range;
    auto span = header.substr(0, slash);
    parse(header.substr(slash + 1), range.total);
    if (span != "*")
    {
        auto dash = span.find('-');
        if (dash != std::string_view::npos)
        {
            parse(span.substr(0, dash), range.first);
            parse(span.substr(dash + 1), range.last);
        }
    }
    return range;
}

std::unique_ptr<Source> createHttpSource(const SourceConfig& config)
{
    return std::make_unique<HttpSource>(config);
}

} // namespace dv
