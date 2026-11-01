#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dv {

// Log file formats that carry their own time, pid and process name columns
enum class LogFormat
{
    Text,         // plain text, every line is a message
    DbLog,        // DebugView++ .dblog: time \t YYYY/MM/DD HH:MM:SS.mmm \t pid \t process \t message
    Sysinternals, // Sysinternals DebugView .log: line \t time \t [pid] message
};

const char* toText(LogFormat format);

inline constexpr std::string_view dbLogIdentification1 = "File Identification Header, DebugView++ Format Version 1";
inline constexpr std::string_view dbLogIdentification2 = "File Identification Header, DebugView++ Format Version 2";

// firstLine/secondLine are the first two decoded lines of the file; extension includes the dot, e.g. ".log"
LogFormat detectLogFormat(std::string_view firstLine, std::string_view secondLine, std::string_view extension);
bool isDbLogHeader(std::string_view line);

struct ParsedLine
{
    double relativeSeconds = 0; // valid when hasRelativeTime
    int64_t systemNs = 0;       // valid when hasSystemTime
    bool hasRelativeTime = false;
    bool hasSystemTime = false;
    uint32_t pid = 0;
    std::string_view processName;
    std::string_view message;
};

bool parseDbLogLine(std::string_view line, ParsedLine& out);

// keeps the date for Sysinternals logs, which only contain the time of day
class SysinternalsParser
{
public:
    bool parse(std::string_view line, ParsedLine& out);

private:
    int64_t m_dayBaseNs = 0;
    int64_t m_lastTimeOfDayNs = -1;
    bool m_haveBase = false;
};

// writer for the DebugView++ format; the result is UTF-8
std::string dbLogHeaderLine(int64_t systemNs);
void appendDbLogLine(std::string& out, double relativeSeconds, int64_t systemNs, uint32_t pid, std::string_view processName, std::string_view message);

// FILETIME (100ns ticks since 1601, UTC) <-> nanoseconds since the Unix epoch
int64_t fileTimeToUnixNs(uint64_t fileTime);
uint64_t unixNsToFileTime(int64_t unixNs);

} // namespace dv
