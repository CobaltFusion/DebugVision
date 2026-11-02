// dvtest: generates log lines with nested function calls (spans) and singular events, in real time, to try out
// the DebugVision timeline and filters

#include "dvcore/Platform.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

const char* const usage = R"(dvtest - generates nested function calls and events, to try out the DebugVision timeline

Usage: dvtest [options]

What to generate:
  -n, --count <N>      number of top-level calls (default 20)
  --endless            keep generating until stopped with Ctrl+C
  --style <styles>     begin/end style of the calls, comma-separated (default: all, a random style per call):
                         enter-leave  "parse - Enter"  ... "parse - Leave"
                         begin-end    "Begin - parse"  ... "End - parse"
                         enter-exit   "Enter parse"    ... "Exit parse"
  --depth <N>          maximum nesting depth of calls (default 4)
  --events <rate>      random singular events (IRQ, GC pause, cache miss), <rate> per second on average
  --tick <ms>          a regular "Tick <n>" event every <ms> milliseconds
Inside calls "<function> - Event occurred" and, now and then, "<function> - Exception" lines are written.

Output (default: standard output, e.g. dvtest --endless | debugvision):
  --udp [host:]port    send every line as a UDP datagram (host default 127.0.0.1)
  --ods                write with OutputDebugString (Windows)
  --speed <factor>     time scale: 2 runs twice as fast, 0 writes everything without waiting (default 1)
  --seed <N>           random seed, for a repeatable sequence

  -h, --help           show this text
)";

enum class Style
{
    EnterLeave,
    BeginEnd,
    EnterExit,
};

const std::vector<std::string> functions = {"main_loop", "parse", "tokenize", "render", "load_texture", "decode", "compress",
    "send_packet", "update_physics", "query_db", "resolve_names", "flush_cache"};

const std::vector<std::string> randomEvents = {"IRQ 5", "IRQ 11", "GC pause", "cache miss"};

struct Options
{
    long long count = 20;
    bool endless = false;
    std::vector<Style> styles = {Style::EnterLeave, Style::BeginEnd, Style::EnterExit};
    int depth = 4;
    double eventsPerSecond = 0;
    double tickMs = 0;
    std::string udpHost;
    uint16_t udpPort = 0;
    bool ods = false;
    double speed = 1.0;
    unsigned seed = std::random_device{}();
};

struct Line
{
    double atMs; // since the start of the sequence
    std::string text;
};

class Generator
{
public:
    explicit Generator(const Options& options) :
        m_options(options),
        m_random(options.seed)
    {
    }

    // one top-level call, starting at 'startMs'; returns the lines in time order and moves 'startMs' past the call
    std::vector<Line> next(double& startMs)
    {
        std::vector<Line> lines;
        auto style = m_options.styles[pick(m_options.styles.size())];
        auto end = call(functions[pick(functions.size())], style, 1, startMs, lines);
        startMs = end + uniform(2, 20);
        addSingularEvents(startMs, lines);
        std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.atMs < b.atMs; });
        return lines;
    }

private:
    size_t pick(size_t n) { return std::uniform_int_distribution<size_t>(0, n - 1)(m_random); }
    double uniform(double from, double to) { return std::uniform_real_distribution<double>(from, to)(m_random); }
    bool chance(double p) { return std::bernoulli_distribution(p)(m_random); }

    static std::string begin(const std::string& f, Style style)
    {
        switch (style)
        {
        case Style::EnterLeave: return f + " - Enter";
        case Style::BeginEnd: return "Begin - " + f;
        case Style::EnterExit: return "Enter " + f;
        }
        return f;
    }

    static std::string end(const std::string& f, Style style)
    {
        switch (style)
        {
        case Style::EnterLeave: return f + " - Leave";
        case Style::BeginEnd: return "End - " + f;
        case Style::EnterExit: return "Exit " + f;
        }
        return f;
    }

    // writes a call with random nested calls and events; returns the time it ends
    double call(const std::string& f, Style style, int depth, double t, std::vector<Line>& lines)
    {
        lines.push_back({t, begin(f, style)});
        t += uniform(0.5, 3);
        auto children = depth < m_options.depth ? pick(4) : 0;
        for (size_t i = 0; i < children; ++i)
        {
            std::string child;
            do
                child = functions[pick(functions.size())];
            while (child == f);
            t = call(child, style, depth + 1, t, lines) + uniform(0.2, 2);
        }
        if (chance(0.3))
        {
            lines.push_back({t, f + " - Event occurred"});
            t += uniform(0.2, 2);
        }
        if (chance(0.05))
        {
            lines.push_back({t, f + " - Exception"});
            t += uniform(0.2, 1);
        }
        t += uniform(1, 10);
        lines.push_back({t, end(f, style)});
        return t;
    }

    // the random events (exponentially distributed gaps: on average the given rate) and the regular ticks up to 'to',
    // continuing where the previous call left off, so the time between calls is covered as well
    void addSingularEvents(double to, std::vector<Line>& lines)
    {
        if (m_options.eventsPerSecond > 0)
        {
            std::exponential_distribution<double> gap(m_options.eventsPerSecond / 1000.0);
            for (; m_nextRandomEvent < to; m_nextRandomEvent += gap(m_random))
                lines.push_back({m_nextRandomEvent, randomEvents[pick(randomEvents.size())]});
        }
        if (m_options.tickMs > 0)
        {
            for (; m_nextTick < to; m_nextTick += m_options.tickMs)
                lines.push_back({m_nextTick, "Tick " + std::to_string(++m_ticks)});
        }
    }

    const Options& m_options;
    std::mt19937 m_random;
    double m_nextRandomEvent = 0;
    double m_nextTick = 0;
    long long m_ticks = 0;
};

class Output
{
public:
    explicit Output(const Options& options) :
        m_options(options)
    {
        if (m_options.udpPort != 0)
        {
            // one socket for all lines: a sender port per line would be a process (and a timeline lane) per line
            std::string error;
            m_udp = dv::platform::UdpSender::open(m_options.udpHost, m_options.udpPort, error);
            if (!m_udp)
                throw std::runtime_error(error);
        }
    }

    void write(const std::string& line)
    {
        if (m_udp)
        {
            std::string error;
            if (!m_udp->send(line, error))
                throw std::runtime_error(error);
            return;
        }
#ifdef _WIN32
        if (m_options.ods)
        {
            OutputDebugStringA((line + "\n").c_str());
            return;
        }
#endif
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

private:
    const Options& m_options;
    std::unique_ptr<dv::platform::UdpSender> m_udp;
};

std::vector<Style> parseStyles(const std::string& text)
{
    std::vector<Style> styles;
    size_t pos = 0;
    while (pos <= text.size())
    {
        auto comma = text.find(',', pos);
        auto name = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? text.size() + 1 : comma + 1;
        if (name == "all")
            styles.insert(styles.end(), {Style::EnterLeave, Style::BeginEnd, Style::EnterExit});
        else if (name == "enter-leave")
            styles.push_back(Style::EnterLeave);
        else if (name == "begin-end")
            styles.push_back(Style::BeginEnd);
        else if (name == "enter-exit")
            styles.push_back(Style::EnterExit);
        else
            throw std::runtime_error("unknown style '" + name + "', see --help");
    }
    return styles;
}

double parseNumber(const std::string& option, const std::string& text)
{
    char* end = nullptr;
    auto value = std::strtod(text.c_str(), &end);
    if (text.empty() || *end != '\0' || value < 0)
        throw std::runtime_error(option + " expects a number >= 0, not '" + text + "'");
    return value;
}

Options parseOptions(int argc, char* argv[])
{
    Options options;
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
            std::exit(0);
        }
        else if (arg == "-n" || arg == "--count")
            options.count = static_cast<long long>(parseNumber(arg, value()));
        else if (arg == "--endless")
            options.endless = true;
        else if (arg == "--style")
            options.styles = parseStyles(value());
        else if (arg == "--depth")
            options.depth = std::max(1, static_cast<int>(parseNumber(arg, value())));
        else if (arg == "--events")
            options.eventsPerSecond = parseNumber(arg, value());
        else if (arg == "--tick")
            options.tickMs = parseNumber(arg, value());
        else if (arg == "--speed")
            options.speed = parseNumber(arg, value());
        else if (arg == "--seed")
            options.seed = static_cast<unsigned>(parseNumber(arg, value()));
        else if (arg == "--ods")
        {
#ifdef _WIN32
            options.ods = true;
#else
            throw std::runtime_error("--ods is only available on Windows");
#endif
        }
        else if (arg == "--udp")
        {
            auto target = value();
            auto colon = target.rfind(':');
            options.udpHost = colon == std::string::npos ? "127.0.0.1" : target.substr(0, colon);
            auto port = parseNumber(arg, colon == std::string::npos ? target : target.substr(colon + 1));
            if (port < 1 || port > 65535)
                throw std::runtime_error("--udp expects a port between 1 and 65535");
            options.udpPort = static_cast<uint16_t>(port);
        }
        else
            throw std::runtime_error("unknown option '" + arg + "', see --help");
    }
    return options;
}

} // namespace

int main(int argc, char* argv[])
{
    try
    {
        auto options = parseOptions(argc, argv);
        Generator generator(options);
        Output output(options);
        auto start = std::chrono::steady_clock::now();
        double t = 0;
        for (long long n = 0; options.endless || n < options.count; ++n)
        {
            for (auto& line : generator.next(t))
            {
                if (options.speed > 0)
                    std::this_thread::sleep_until(start + std::chrono::microseconds(static_cast<int64_t>(line.atMs * 1000 / options.speed)));
                output.write(line.text);
            }
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "dvtest: %s\n", e.what());
        return 1;
    }
    return 0;
}
