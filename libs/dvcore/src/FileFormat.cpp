#include "dvcore/FileFormat.h"

#include "dvcore/Clock.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace dv {

namespace {

constexpr int64_t nsPerSecond = 1'000'000'000;
constexpr int64_t nsPerDay = 86'400 * nsPerSecond;
constexpr uint64_t fileTimeUnixEpoch = 116444736000000000ull; // 1970-01-01 in FILETIME ticks

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

// splits off the next tab separated column
std::string_view nextColumn(std::string_view& rest)
{
    auto tab = rest.find('\t');
    auto col = rest.substr(0, tab);
    rest = tab == std::string_view::npos ? std::string_view() : rest.substr(tab + 1);
    return col;
}

template <typename T>
bool parseNumber(std::string_view text, T& value)
{
    text = trim(text);
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && ptr == text.data() + text.size();
}

bool parseDouble(std::string_view text, double& value)
{
    text = trim(text);
    if (text.empty())
        return false;
    // std::from_chars for double is not available on all supported standard libraries (Apple libc++)
    std::string copy(text);
    char* end = nullptr;
    value = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() + copy.size();
}

// parses "HH:MM:SS" or "HH:MM:SS.mmm", optionally followed by " AM"/" PM"; returns nanoseconds since midnight
bool parseTimeOfDay(std::string_view text, int64_t& ns)
{
    text = trim(text);
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millis = 0;
    bool pm = false;
    bool am = false;
    if (text.size() >= 3 && (equalsIgnoreCase(text.substr(text.size() - 2), "AM") || equalsIgnoreCase(text.substr(text.size() - 2), "PM")))
    {
        pm = equalsIgnoreCase(text.substr(text.size() - 2), "PM");
        am = !pm;
        text = trim(text.substr(0, text.size() - 2));
    }
    auto c1 = text.find(':');
    auto c2 = c1 == std::string_view::npos ? c1 : text.find(':', c1 + 1);
    if (c2 == std::string_view::npos)
        return false;
    auto dot = text.find('.', c2);
    if (!parseNumber(text.substr(0, c1), hour) || !parseNumber(text.substr(c1 + 1, c2 - c1 - 1), minute))
        return false;
    if (!parseNumber(text.substr(c2 + 1, dot == std::string_view::npos ? std::string_view::npos : dot - c2 - 1), second))
        return false;
    if (dot != std::string_view::npos)
    {
        auto frac = text.substr(dot + 1);
        if (frac.empty() || frac.size() > 9 || !parseNumber(frac, millis))
            return false;
        for (auto n = frac.size(); n < 3; ++n)
            millis *= 10;
        for (auto n = frac.size(); n > 3; --n)
            millis /= 10;
    }
    if (am || pm)
    {
        if (hour < 1 || hour > 12)
            return false;
        hour = hour % 12 + (pm ? 12 : 0);
    }
    if (hour > 23 || minute > 59 || second > 60)
        return false;
    ns = ((hour * 3600LL + minute * 60LL + second) * 1000LL + millis) * 1'000'000LL;
    return true;
}

// "YYYY/MM/DD HH:MM:SS.mmm" in local time
bool parseLocalDateTime(std::string_view text, int64_t& systemNs)
{
    text = trim(text);
    auto space = text.find(' ');
    if (space == std::string_view::npos)
        return false;
    auto date = text.substr(0, space);
    auto s1 = date.find('/');
    auto s2 = s1 == std::string_view::npos ? s1 : date.find('/', s1 + 1);
    if (s2 == std::string_view::npos)
        return false;
    LocalTimeParts parts;
    if (!parseNumber(date.substr(0, s1), parts.year) || !parseNumber(date.substr(s1 + 1, s2 - s1 - 1), parts.month) || !parseNumber(date.substr(s2 + 1), parts.day))
        return false;
    int64_t tod = 0;
    if (!parseTimeOfDay(text.substr(space + 1), tod))
        return false;
    parts.hour = static_cast<int>(tod / (3600 * nsPerSecond));
    parts.minute = static_cast<int>(tod / (60 * nsPerSecond) % 60);
    parts.second = static_cast<int>(tod / nsPerSecond % 60);
    parts.millisecond = static_cast<int>(tod / 1'000'000 % 1000);
    systemNs = fromLocalTime(parts);
    return systemNs != 0;
}

int64_t localMidnight(int64_t systemNs)
{
    auto p = toLocalTime(systemNs);
    p.hour = 0;
    p.minute = 0;
    p.second = 0;
    p.millisecond = 0;
    return fromLocalTime(p);
}

} // namespace

const char* toText(LogFormat format)
{
    switch (format)
    {
    case LogFormat::Text: return "text";
    case LogFormat::DbLog: return "dblog";
    case LogFormat::Sysinternals: return "sysinternals";
    }
    return "text";
}

bool isDbLogHeader(std::string_view line)
{
    line = trim(line);
    return endsWith(line, dbLogIdentification1) || endsWith(line, dbLogIdentification2);
}

LogFormat detectLogFormat(std::string_view firstLine, std::string_view secondLine, std::string_view extension)
{
    if (isDbLogHeader(firstLine))
        return LogFormat::DbLog;
    if (equalsIgnoreCase(extension, ".dblog"))
        return LogFormat::DbLog;
    if (equalsIgnoreCase(extension, ".log"))
    {
        // the first line of a Sysinternals log may contain the computer name, so check the second line:
        // process messages have 3 tab separated columns + [pid], kernel messages 3 columns
        auto tabs = std::count(secondLine.begin(), secondLine.end(), '\t');
        if (tabs == 2 || tabs == 3)
            return LogFormat::Sysinternals;
    }
    return LogFormat::Text;
}

bool parseDbLogLine(std::string_view line, ParsedLine& out)
{
    auto rest = line;
    auto time = nextColumn(rest);
    auto date = nextColumn(rest);
    auto pid = nextColumn(rest);
    auto process = nextColumn(rest);
    if (pid.empty() && process.empty())
        return false;
    out = ParsedLine{};
    out.hasRelativeTime = parseDouble(time, out.relativeSeconds);
    if (date.find('/') != std::string_view::npos)
    {
        out.hasSystemTime = parseLocalDateTime(date, out.systemNs);
    }
    else
    {
        uint64_t fileTime = 0;
        if (parseNumber(date, fileTime) && fileTime > fileTimeUnixEpoch)
        {
            out.systemNs = fileTimeToUnixNs(fileTime);
            out.hasSystemTime = true;
        }
    }
    parseNumber(pid, out.pid);
    out.processName = process;
    out.message = rest;
    return true;
}

bool SysinternalsParser::parse(std::string_view line, ParsedLine& out)
{
    auto rest = line;
    nextColumn(rest); // line number
    auto time = nextColumn(rest);
    if (time.empty())
        return false;
    out = ParsedLine{};

    int64_t tod = 0;
    if (parseTimeOfDay(time, tod))
    {
        if (!m_haveBase)
        {
            // Sysinternals logs contain only the time of day; assume the log is from today
            m_dayBaseNs = localMidnight(nowSystemNs());
            m_haveBase = true;
        }
        if (m_lastTimeOfDayNs >= 0 && tod + 3600 * nsPerSecond < m_lastTimeOfDayNs)
            m_dayBaseNs += nsPerDay; // the time wrapped past midnight
        m_lastTimeOfDayNs = tod;
        out.systemNs = m_dayBaseNs + tod;
        out.hasSystemTime = true;
    }
    else
    {
        out.hasRelativeTime = parseDouble(time, out.relativeSeconds);
    }

    if (!rest.empty() && rest.front() == '[')
    {
        auto close = rest.find("] ");
        if (close != std::string_view::npos && parseNumber(rest.substr(1, close - 1), out.pid))
        {
            out.processName = "[unavailable]";
            out.message = rest.substr(close + 2);
            return true;
        }
    }
    out.processName = "[kernel]";
    out.message = rest;
    return true;
}

std::string dbLogHeaderLine(int64_t systemNs)
{
    std::string line;
    appendDbLogLine(line, 0, systemNs, 0, "DebugVision", dbLogIdentification1);
    return line;
}

void appendDbLogLine(std::string& out, double relativeSeconds, int64_t systemNs, uint32_t pid, std::string_view processName, std::string_view message)
{
    while (!message.empty() && (message.back() == ' ' || message.back() == '\r' || message.back() == '\n' || message.back() == '\t'))
        message.remove_suffix(1);
    auto p = toLocalTime(systemNs);
    char buf[96];
    auto n = std::snprintf(buf, sizeof(buf), "%.6f\t%04d/%02d/%02d %02d:%02d:%02d.%03d\t%u\t", relativeSeconds, p.year, p.month, p.day, p.hour, p.minute, p.second, p.millisecond, pid);
    out.append(buf, static_cast<size_t>(n));
    out.append(processName);
    out.push_back('\t');
    out.append(message);
    out.push_back('\n');
}

int64_t fileTimeToUnixNs(uint64_t fileTime)
{
    return static_cast<int64_t>(fileTime - fileTimeUnixEpoch) * 100;
}

uint64_t unixNsToFileTime(int64_t unixNs)
{
    return static_cast<uint64_t>(unixNs / 100) + fileTimeUnixEpoch;
}

} // namespace dv
