#include "dvcore/Clock.h"

#include <cstdio>
#include <ctime>

namespace dv {

namespace {

std::tm localTm(time_t t)
{
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

int64_t floorDiv(int64_t a, int64_t b)
{
    auto q = a / b;
    if ((a % b) != 0 && ((a < 0) != (b < 0)))
        --q;
    return q;
}

} // namespace

LocalTimeParts toLocalTime(int64_t systemNs)
{
    auto seconds = floorDiv(systemNs, 1'000'000'000);
    auto ms = static_cast<int>((systemNs - seconds * 1'000'000'000) / 1'000'000);
    auto tm = localTm(static_cast<time_t>(seconds));
    return LocalTimeParts{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms};
}

int64_t fromLocalTime(const LocalTimeParts& parts)
{
    std::tm tm{};
    tm.tm_year = parts.year - 1900;
    tm.tm_mon = parts.month - 1;
    tm.tm_mday = parts.day;
    tm.tm_hour = parts.hour;
    tm.tm_min = parts.minute;
    tm.tm_sec = parts.second;
    tm.tm_isdst = -1;
    auto t = std::mktime(&tm);
    if (t == static_cast<time_t>(-1))
        return 0;
    return static_cast<int64_t>(t) * 1'000'000'000 + static_cast<int64_t>(parts.millisecond) * 1'000'000;
}

std::string formatClockTime(int64_t systemNs)
{
    auto p = toLocalTime(systemNs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", p.hour, p.minute, p.second, p.millisecond);
    return buf;
}

std::string formatDate(int64_t systemNs)
{
    auto p = toLocalTime(systemNs);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d/%02d/%02d", p.year, p.month, p.day);
    return buf;
}

std::string formatRelativeTime(int64_t steadyNs, int64_t baseSteadyNs)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(steadyNs - baseSteadyNs) / 1e9);
    return buf;
}

} // namespace dv
