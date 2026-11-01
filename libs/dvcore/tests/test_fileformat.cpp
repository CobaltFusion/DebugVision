#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"

#include <doctest/doctest.h>

using namespace dv;

TEST_CASE("detect log formats")
{
    CHECK(detectLogFormat("0.000000\t2024/01/01 10:00:00.000\t0\tDebugView++.exe\tFile Identification Header, DebugView++ Format Version 1", "", ".log") == LogFormat::DbLog);
    CHECK(detectLogFormat("COMPUTER", "1\t0.00000000\t[1234] hello", ".log") == LogFormat::Sysinternals);
    CHECK(detectLogFormat("COMPUTER", "1\t0.00000000\t[1234] hello", ".txt") == LogFormat::Text);
    CHECK(detectLogFormat("just text", "more text", ".log") == LogFormat::Text);
}

TEST_CASE("dblog write and parse round trip")
{
    auto now = nowSystemNs();
    now -= now % 1'000'000; // millisecond resolution
    std::string line;
    appendDbLogLine(line, 1.25, now, 1234, "app.exe", "message\twith tab\r\n");
    CHECK(line.back() == '\n');
    line.pop_back();
    ParsedLine parsed;
    REQUIRE(parseDbLogLine(line, parsed));
    CHECK(parsed.relativeSeconds == doctest::Approx(1.25));
    CHECK(parsed.hasSystemTime);
    CHECK(parsed.systemNs == now);
    CHECK(parsed.pid == 1234);
    CHECK(parsed.processName == "app.exe");
    CHECK(parsed.message == "message\twith tab");
}

TEST_CASE("dblog v1 FILETIME column")
{
    ParsedLine parsed;
    REQUIRE(parseDbLogLine("0.5\t133500000000000000\t7\tp\tmsg", parsed));
    CHECK(parsed.hasSystemTime);
    CHECK(parsed.systemNs == fileTimeToUnixNs(133500000000000000ull));
}

TEST_CASE("sysinternals lines")
{
    SysinternalsParser parser;
    ParsedLine p;
    REQUIRE(parser.parse("1\t10:15:30.250\t[42] hello world", p));
    CHECK(p.pid == 42);
    CHECK(p.message == "hello world");
    CHECK(p.processName == "[unavailable]");
    CHECK(p.hasSystemTime);
    auto t = toLocalTime(p.systemNs);
    CHECK(t.hour == 10);
    CHECK(t.minute == 15);
    CHECK(t.millisecond == 250);

    REQUIRE(parser.parse("2\t11:15:30 PM\tkernel message", p));
    CHECK(p.processName == "[kernel]");
    CHECK(toLocalTime(p.systemNs).hour == 23);

    // past midnight: the date advances
    auto before = p.systemNs;
    REQUIRE(parser.parse("3\t00:00:01.000\t[1] next day", p));
    CHECK(p.systemNs > before);

    REQUIRE(parser.parse("4\t1.500000\t[1] relative", p));
    CHECK(p.hasRelativeTime);
    CHECK(p.relativeSeconds == doctest::Approx(1.5));
}
