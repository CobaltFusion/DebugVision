#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace dv {

// All timestamps are stored as int64 nanoseconds:
//  - steady: std::chrono::steady_clock, for relative time display (monotonic)
//  - system: nanoseconds since the Unix epoch (UTC), for wall-clock display

inline int64_t nowSteadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline int64_t nowSystemNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct LocalTimeParts
{
    int year = 0;
    int month = 0; // 1..12
    int day = 0;   // 1..31
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
};

LocalTimeParts toLocalTime(int64_t systemNs);
int64_t fromLocalTime(const LocalTimeParts& parts);

// "HH:MM:SS.mmm" in local time
std::string formatClockTime(int64_t systemNs);
// "YYYY/MM/DD" in local time
std::string formatDate(int64_t systemNs);
// "%.6f" seconds relative to 'baseSteadyNs'
std::string formatRelativeTime(int64_t steadyNs, int64_t baseSteadyNs);

} // namespace dv
