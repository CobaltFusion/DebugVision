// dvcli: the command line version of DebugVision (the equivalent of DebugView++'s DebugViewConsole)

#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"
#include "dvcore/FilterEngine.h"
#include "dvcore/Ingest.h"
#include "dvcore/LineStore.h"
#include "dvcore/Platform.h"

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

const char* const usage = R"(dvcli - capture and filter debug output

Usage:
  dvcli [options] [-c] [-d <file>] [--source <spec>]... [--udp <port>]... [--file <path>]... [--stdin]
  dvcli -x      stop all running dvcli instances (not available on macOS, use 'pkill dvcli')
  dvcli -u <message> [<port>]   send <message> as a UDP broadcast (default port 2020)
  dvcli -h | --help

Output:
  -c                   write to the console
  -d <file>            write a DebugView++ compatible log file (.dblog)
  -f                   flush after every line
Columns:
  -l                   line number
  -s                   system time (HH:MM:SS.mmm)
  -q                   relative time in seconds
  -p                   process id
  -n                   process name
  -t                   separate columns with tabs instead of spaces
Capture:
  -a                   auto newline: every message is a line of its own (default on; -a- turns it off)
  --source <spec>      add a source, e.g. udp:port=2020,bind=0.0.0.0  file:path=app.log,tail=1
                       process:command=make,arguments=-j8;all   tcp-listen:port=2021
  --udp <port>         shorthand for --source udp:port=<port>
  --file <path>        shorthand for --source file:path=<path>
  --stdin              read standard input
  --list-sources       show the available source types
Filters (case-insensitive text):
  -i <text>            include messages containing <text>
  -e <text>            exclude messages containing <text>
  --include-process <text>
  --exclude-process <text>
  -m <text>            quit when a (filtered) message contains <text>
  -v                   verbose
)";

#ifdef _WIN32
const char* const sourceNote = "On Windows, OutputDebugString is captured when no other source is given.\n";
#else
const char* const sourceNote = "At least one source is required (OutputDebugString exists only on Windows).\n";
#endif

struct Options
{
    bool console = false;
    std::string logFile;
    bool flush = false;
    bool lineNumber = false;
    bool systemTime = false;
    bool relativeTime = false;
    bool pid = false;
    bool processName = false;
    bool tabs = false;
    bool autoNewline = true;
    bool verbose = false;
    bool readStdin = false;
    std::vector<std::string> sources;
    dv::FilterSet filters;
    std::string quitMessage;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<bool> g_quit{false};
bool g_signaled = false;

void requestQuit()
{
    g_quit = true;
    {
        std::lock_guard lock(g_mutex);
        g_signaled = true;
    }
    g_cv.notify_one();
}

void onSignal(int)
{
    // only async-signal-safe work here: the main loop polls g_quit at least every 250ms
    g_quit = true;
}

void addTextFilter(dv::FilterSet& set, bool process, dv::FilterType type, const std::string& text)
{
    dv::Filter f;
    f.text = text;
    f.filterType = type;
    (process ? set.processFilters : set.messageFilters).push_back(f);
}

int sendBroadcast(int argc, char* argv[], int i)
{
    if (i + 1 >= argc)
    {
        std::fprintf(stderr, "dvcli: -u requires a message\n");
        return 1;
    }
    std::string message = argv[i + 1];
    int port = i + 2 < argc ? std::atoi(argv[i + 2]) : 2020;
    std::string error;
    if (!dv::platform::sendUdp("255.255.255.255", static_cast<uint16_t>(port), message, error))
    {
        std::fprintf(stderr, "dvcli: %s\n", error.c_str());
        return 1;
    }
    return 0;
}

// -x: stop all running instances
#ifdef _WIN32
const wchar_t* const quitEventName = L"DebugVisionCliQuitEvent";

int stopAllInstances()
{
    auto event = CreateEventW(nullptr, TRUE, FALSE, quitEventName);
    SetEvent(event);
    Sleep(200);
    ResetEvent(event);
    CloseHandle(event);
    return 0;
}

void watchQuitRequests(dv::platform::EventLoop& loop)
{
    auto event = CreateEventW(nullptr, TRUE, FALSE, quitEventName);
    if (event)
        loop.watch(event, [] { requestQuit(); });
}
#elif defined(__linux__)
std::filesystem::path runtimeDirectory()
{
    if (auto dir = std::getenv("XDG_RUNTIME_DIR"); dir && *dir)
        return dir;
    return std::filesystem::temp_directory_path();
}

int stopAllInstances()
{
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(runtimeDirectory(), ec))
    {
        if (entry.path().filename().string().rfind("dvcli-quit-", 0) != 0)
            continue;
        auto fd = ::open(entry.path().c_str(), O_WRONLY | O_NONBLOCK);
        if (fd < 0)
        {
            std::filesystem::remove(entry.path(), ec); // stale
            continue;
        }
        [[maybe_unused]] auto n = ::write(fd, "q", 1);
        ::close(fd);
    }
    return 0;
}

std::filesystem::path g_quitFifo;

void watchQuitRequests(dv::platform::EventLoop& loop)
{
    g_quitFifo = runtimeDirectory() / ("dvcli-quit-" + std::to_string(::getpid()));
    ::mkfifo(g_quitFifo.c_str(), 0600);
    auto fd = ::open(g_quitFifo.c_str(), O_RDWR | O_NONBLOCK); // O_RDWR: no EOF storm without writers
    if (fd >= 0)
        loop.watch(fd, [] { requestQuit(); });
}
#else
int stopAllInstances()
{
    std::fprintf(stderr, "dvcli: -x is not available on this platform, use 'pkill dvcli'\n");
    return 1;
}

void watchQuitRequests(dv::platform::EventLoop&) {}
#endif

class Output
{
public:
    explicit Output(const Options& options) :
        m_options(options),
        m_separator(options.tabs ? "\t" : " ")
    {
        if (!options.logFile.empty())
        {
            m_log.open(dv::platform::utf8ToPath(options.logFile), std::ios::binary | std::ios::trunc);
            if (!m_log)
                throw std::runtime_error("cannot write '" + options.logFile + "'");
            m_log << "\xEF\xBB\xBF" << dv::dbLogHeaderLine(dv::nowSystemNs());
        }
        m_console = dv::platform::isConsole(1);
    }

    void write(uint64_t lineNumber, const dv::LineRecord& rec, std::string_view process, std::string_view message, int64_t baseSteadyNs)
    {
        double relative = static_cast<double>(rec.steadyNs - baseSteadyNs) / 1e9;
        if (m_options.console)
        {
            m_buffer.clear();
            auto column = [&](const std::string& text) {
                m_buffer += text;
                m_buffer += m_separator;
            };
            if (m_options.lineNumber)
                column(std::to_string(lineNumber));
            if (m_options.systemTime)
                column(dv::formatClockTime(rec.systemNs));
            if (m_options.relativeTime)
                column(dv::formatRelativeTime(rec.steadyNs, baseSteadyNs));
            if (m_options.pid)
                column(std::to_string(rec.pid));
            if (m_options.processName)
                column(std::string(process));
            m_buffer.append(message);
            m_buffer.push_back('\n');
            m_pending += m_buffer;
        }
        if (m_log.is_open())
        {
            m_logBuffer.clear();
            dv::appendDbLogLine(m_logBuffer, relative, rec.systemNs, rec.pid, process, message);
            m_log << m_logBuffer;
        }
        if (m_options.flush)
            flush();
    }

    void flush()
    {
        if (!m_pending.empty())
        {
            if (m_console)
                dv::platform::writeConsole(1, m_pending);
            else
                std::fwrite(m_pending.data(), 1, m_pending.size(), stdout);
            m_pending.clear();
        }
        std::fflush(stdout);
        if (m_log.is_open())
            m_log.flush();
    }

private:
    const Options& m_options;
    std::string m_separator;
    std::ofstream m_log;
    bool m_console = false;
    std::string m_buffer;
    std::string m_pending;
    std::string m_logBuffer;
};

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    auto& registry = dv::SourceRegistry::instance();
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (i + 1 >= argc)
                    throw std::runtime_error("option " + arg + " requires a value");
                return argv[++i];
            };
            if (arg == "-h" || arg == "--help")
            {
                std::fputs(usage, stdout);
                std::fputs(sourceNote, stdout);
                return 0;
            }
            if (arg == "-x")
                return stopAllInstances();
            if (arg == "-u")
                return sendBroadcast(argc, argv, i);
            if (arg == "--list-sources")
            {
                for (auto& t : registry.types())
                    std::printf("%-18s %-28s %s\n", t.type.c_str(), t.displayName.c_str(), t.defaultParams.dump().c_str());
                return 0;
            }
            if (arg == "-c") options.console = true;
            else if (arg == "-d") options.logFile = value();
            else if (arg == "-f") options.flush = true;
            else if (arg == "-l") options.lineNumber = true;
            else if (arg == "-s") options.systemTime = true;
            else if (arg == "-q") options.relativeTime = true;
            else if (arg == "-p") options.pid = true;
            else if (arg == "-n") options.processName = true;
            else if (arg == "-t") options.tabs = true;
            else if (arg == "-a") options.autoNewline = true;
            else if (arg == "-a-") options.autoNewline = false;
            else if (arg == "-v") options.verbose = true;
            else if (arg == "-i") addTextFilter(options.filters, false, dv::FilterType::Include, value());
            else if (arg == "-e") addTextFilter(options.filters, false, dv::FilterType::Exclude, value());
            else if (arg == "--include-process") addTextFilter(options.filters, true, dv::FilterType::Include, value());
            else if (arg == "--exclude-process") addTextFilter(options.filters, true, dv::FilterType::Exclude, value());
            else if (arg == "-m") options.quitMessage = value();
            else if (arg == "--source") options.sources.push_back(value());
            else if (arg == "--udp") options.sources.push_back("udp:port=" + value());
            else if (arg == "--file") options.sources.push_back("file:path=" + value());
            else if (arg == "--stdin") options.readStdin = true;
            else
            {
                // combined short flags, e.g. -clsqpn
                if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-' && arg.find_first_not_of("cflsqpntav", 1) == std::string::npos)
                {
                    for (size_t k = 1; k < arg.size(); ++k)
                    {
                        switch (arg[k])
                        {
                        case 'c': options.console = true; break;
                        case 'f': options.flush = true; break;
                        case 'l': options.lineNumber = true; break;
                        case 's': options.systemTime = true; break;
                        case 'q': options.relativeTime = true; break;
                        case 'p': options.pid = true; break;
                        case 'n': options.processName = true; break;
                        case 't': options.tabs = true; break;
                        case 'a': options.autoNewline = true; break;
                        case 'v': options.verbose = true; break;
                        default: break;
                        }
                    }
                    continue;
                }
                throw std::runtime_error("unknown option '" + arg + "', see --help");
            }
        }

        if (!options.console && options.logFile.empty())
            throw std::runtime_error("specify -c (console output) and/or -d <file>, see --help");

        std::vector<dv::SourceConfig> configs;
        for (auto& spec : options.sources)
            configs.push_back(registry.parseSpec(spec));
        if (options.readStdin)
            configs.push_back(registry.makeDefault("stdin"));
        if (configs.empty())
        {
#ifdef _WIN32
            auto ods = registry.makeDefault("outputdebugstring");
            ods.setParams(dv::OutputDebugStringParams{"both"});
            configs.push_back(ods);
#else
            throw std::runtime_error("no source given; use --source, --udp, --file or --stdin");
#endif
        }
        for (auto& c : configs)
            c.autoNewline = options.autoNewline;

        dv::FilterEngine engine(options.filters);
        auto state = engine.makeState();
        Output output(options);

        dv::LineStore store;
        store.setListener([] {
            {
                std::lock_guard lock(g_mutex);
                g_signaled = true;
            }
            g_cv.notify_one();
        });
        dv::Ingest ingest(store);
        watchQuitRequests(ingest.loop());
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);
        ingest.applySources(configs);
        ingest.start();

        auto base = dv::nowSteadyNs();
        uint64_t next = 0;
        uint64_t lineNumber = 0;
        std::vector<const std::string*> names;
        while (!g_quit)
        {
            {
                std::unique_lock lock(g_mutex);
                // the timeout only exists to notice Ctrl+C (a signal handler cannot notify a condition variable safely)
                g_cv.wait_for(lock, std::chrono::milliseconds(250), [] { return g_signaled; });
                g_signaled = false;
            }
            store.consumeSignal();
            // keep memory bounded: dvcli does not need history
            next = std::max(next, store.beginSeq());
            next = store.forEach(next, store.endSeq(), [&](const dv::LineRef& line) {
                auto id = line.record->originId;
                if (id >= names.size())
                    names.resize(id + 1, nullptr);
                if (!names[id])
                    names[id] = &store.origins().get(id).processName;
                auto& process = *names[id];
                if (!options.verbose && process == "[internal]")
                    return;
                if (!engine.evaluate(line.text, process, state).included)
                    return;
                output.write(++lineNumber, *line.record, process, line.text, base);
                if (!options.quitMessage.empty() && line.text.find(options.quitMessage) != std::string_view::npos)
                    g_quit = true;
            });
            output.flush();
            store.crop(next);
        }
        ingest.stop();
        output.flush();
#ifdef __linux__
        if (!g_quitFifo.empty())
        {
            std::error_code ec;
            std::filesystem::remove(g_quitFifo, ec);
        }
#endif
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "dvcli: %s\n", e.what());
        return 1;
    }
    return 0;
}
