#include "dvcore/Timeline.h"

#include <doctest/doctest.h>

using namespace dv;

namespace {

TimelineRule span(std::string begin, std::string end, MatchType match = MatchType::Simple)
{
    TimelineRule r;
    r.kind = TimelineKind::Span;
    r.begin = std::move(begin);
    r.end = std::move(end);
    r.matchType = match;
    return r;
}

TimelineRule event(std::string text, MarkerShape shape = MarkerShape::Lollipop)
{
    TimelineRule r;
    r.kind = TimelineKind::Event;
    r.begin = std::move(text);
    r.shape = shape;
    return r;
}

// feeds lines as (ns, text) of one process; seq = position
void feed(TimelineModel& model, std::initializer_list<std::pair<int64_t, const char*>> lines, const char* process = "app", uint32_t pid = 1)
{
    static uint64_t seq = 0;
    for (auto& [ns, text] : lines)
        model.add(seq++, ns, text, process, pid);
}

} // namespace

TEST_CASE("timeline: a begin and an end line make a span")
{
    TimelineModel model({span("start", "done")});
    feed(model, {{100, "work start"}, {150, "unrelated"}, {300, "work done"}});
    REQUIRE(model.lanes().size() == 1);
    auto& lane = model.lanes()[0];
    CHECK(lane.name == "app");
    REQUIRE(lane.spans.size() == 1);
    CHECK(lane.spans[0].beginNs == 100);
    CHECK(lane.spans[0].endNs == 300);
    CHECK(lane.spans[0].depth == 0);
    CHECK(lane.maxDuration == 200);
    CHECK(model.firstNs() == 100);
    CHECK(model.lastNs() == 300);
}

TEST_CASE("timeline: open spans, unmatched ends and nesting")
{
    TimelineModel model({span("enter", "leave")});
    feed(model, {{10, "leave"}, {20, "enter outer"}, {30, "enter inner"}, {40, "leave inner"}, {50, "enter open"}});
    auto& spans = model.lanes()[0].spans;
    REQUIRE(spans.size() == 3); // the first 'leave' has nothing to close
    CHECK(spans[0].beginNs == 20);
    CHECK(spans[0].endNs == -1); // still open: the inner span was closed, innermost first
    CHECK(spans[0].depth == 0);
    CHECK(spans[1].beginNs == 30);
    CHECK(spans[1].endNs == 40);
    CHECK(spans[1].depth == 1); // overlaps the outer span
    CHECK(spans[2].endNs == -1);
    CHECK(spans[2].depth == 1); // row 1 is free again at 50
    CHECK(model.lanes()[0].depthCount() == 2);
    CHECK(model.openSpans().size() == 2);
}

TEST_CASE("timeline: capture groups pair interleaved spans")
{
    TimelineModel model({span("frame (\\d+) start", "frame (\\d+) done", MatchType::Regex)});
    feed(model, {{0, "Frame 1 start"}, {10, "Frame 2 start"}, {20, "Frame 1 done"}, {30, "Frame 2 done"}});
    auto& spans = model.lanes()[0].spans;
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].endNs == 20);
    CHECK(spans[1].endNs == 30);
    CHECK(spans[0].depth == 0);
    CHECK(spans[1].depth == 1);
}

TEST_CASE("timeline: \\1 in the end pattern refers to the group of the begin pattern")
{
    TimelineModel model({span("Enter (.*)", "Exit \\1", MatchType::Regex)});
    feed(model, {{0, "Enter outer"}, {10, "Enter inner"}, {20, "Exit inner"}, {25, "Exit nothing"}, {30, "Exit outer"}});
    auto& spans = model.lanes()[0].spans;
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].beginNs == 0);
    CHECK(spans[0].endNs == 30);
    CHECK(spans[1].beginNs == 10);
    CHECK(spans[1].endNs == 20);
}

TEST_CASE("timeline: a back reference matches what the begin group matched, not more")
{
    TimelineModel model({span("Enter (\\w+)", "Exit \\1", MatchType::Regex)});
    feed(model, {{0, "Enter foo"}, {5, "Exit foobar"}, {10, "Exit foo, took 10 ns"}});
    auto& spans = model.lanes()[0].spans;
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].endNs == 10); // "foobar" is another name, the text after "foo" does not matter
}

TEST_CASE("timeline: back references compare like the match type, with or without case")
{
    TimelineModel caseless({span("enter (\\w+)", "exit \\1", MatchType::Regex)});
    feed(caseless, {{0, "ENTER Foo"}, {5, "exit foo"}});
    CHECK(caseless.lanes()[0].spans[0].endNs == 5);

    TimelineModel exact({span("enter (\\w+)", "exit \\1", MatchType::RegexCase)});
    feed(exact, {{0, "enter Foo"}, {5, "exit foo"}, {7, "exit Foo"}});
    CHECK(exact.lanes()[0].spans[0].endNs == 7);
}

TEST_CASE("timeline: several back references, in any order")
{
    TimelineModel model({span("(\\w+) calls (\\w+)", "return to \\1 from \\2", MatchType::Regex)});
    feed(model, {{0, "a calls b"}, {1, "c calls b"}, {2, "return to c from b"}, {3, "return to a from b"}});
    auto& spans = model.lanes()[0].spans;
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].endNs == 3);
    CHECK(spans[1].endNs == 2);

    std::vector<size_t> beginGroups;
    std::vector<size_t> endGroups;
    CHECK(TimelineModel::rewriteBackReferences("(\\w+) calls (\\d+)", "back \\2 to \\1", beginGroups, endGroups) == "back (\\d+) to (\\w+)");
    CHECK(beginGroups == std::vector<size_t>{1, 0});
    CHECK(endGroups == std::vector<size_t>{0, 1});
}

TEST_CASE("timeline: rewriting back references")
{
    std::vector<size_t> b;
    std::vector<size_t> e;
    // named groups count, non-capturing groups and lookbehinds do not; escapes and classes are skipped
    CHECK(TimelineModel::rewriteBackReferences("(?:x)(?<=y)\\((?<name>[a-z(]+)\\)", "(end) \\1", b, e) == "(end) ([a-z(]+)");
    CHECK(b == std::vector<size_t>{0});
    CHECK(e == std::vector<size_t>{1}); // the user's own group in end comes first
    b.clear();
    e.clear();
    CHECK(TimelineModel::rewriteBackReferences("(\\w+)", "literal \\\\1 and [\\1]", b, e).empty()); // no references
    CHECK_THROWS_AS(TimelineModel::rewriteBackReferences("(\\w+)", "Exit \\2", b, e), std::runtime_error);
    CHECK_THROWS_AS(TimelineModel::rewriteBackReferences("(\\w+)", "Exit \\12", b, e), std::runtime_error);
    CHECK_THROWS_AS(TimelineModel({span("Enter (\\w+)", "Exit \\2", MatchType::Regex)}), TimelineError);
}

TEST_CASE("timeline: spans pair per process, lanes per process name")
{
    TimelineModel model({span("start", "done")});
    model.add(1, 0, "start", "a", 1);
    model.add(2, 5, "start", "b", 2);
    model.add(3, 10, "done", "a", 1);
    REQUIRE(model.lanes().size() == 2);
    CHECK(model.lanes()[0].name == "a");
    CHECK(model.lanes()[0].spans[0].endNs == 10);
    CHECK(model.lanes()[1].name == "b");
    CHECK(model.lanes()[1].spans[0].endNs == -1);
}

TEST_CASE("timeline: events and disabled rules")
{
    auto disabled = event("tick");
    disabled.enabled = false;
    TimelineModel model({event("irq", MarkerShape::Triangle), disabled});
    feed(model, {{5, "IRQ 7"}, {6, "tick"}, {7, "irq 8"}});
    auto& events = model.lanes()[0].events;
    REQUIRE(events.size() == 2);
    CHECK(events[0].ns == 5);
    CHECK(events[1].ns == 7);
    CHECK(events[0].rule == 0);
}

TEST_CASE("timeline: reset clears the lanes but keeps the rules")
{
    TimelineModel model({span("start", "done")});
    feed(model, {{0, "start"}, {1, "done"}});
    model.reset();
    CHECK(model.lanes().empty());
    CHECK(model.empty());
    CHECK(model.rules().size() == 1);
}

TEST_CASE("timeline: invalid rules identify the rule")
{
    CHECK_THROWS_AS(TimelineModel({span("ok", "ok"), span("(", "x", MatchType::Regex)}), TimelineError);
    try
    {
        TimelineModel model({event("ok"), span("begin only", "")});
        FAIL("expected TimelineError");
    }
    catch (const TimelineError& e)
    {
        CHECK(e.index == 1);
    }
}

TEST_CASE("timeline: rules round-trip through JSON")
{
    auto r = span("frame (\\d+) start", "frame (\\d+) done", MatchType::Regex);
    r.name = "frame";
    r.color = Color::fromRgb(255, 0, 0);
    auto e = event("irq", MarkerShape::Square);
    e.enabled = false;
    nlohmann::json j = std::vector<TimelineRule>{r, e};
    auto back = j.get<std::vector<TimelineRule>>();
    REQUIRE(back.size() == 2);
    CHECK(back[0] == r);
    CHECK(back[1] == e);
}
