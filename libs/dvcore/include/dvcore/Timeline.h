#pragma once

#include "dvcore/Filter.h"
#include "dvcore/FilterEngine.h"
#include "dvcore/Matcher.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace dv {

// What the timeline of a view shows: a Span rule makes a bar from a line matching 'begin' to the next line matching
// 'end' (of the same process; when the patterns have capture groups, the groups must be equal, so
// "frame (\d+) start" pairs with "frame (\d+) done" of the same frame). An Event rule marks single lines.
enum class TimelineKind : int
{
    Span = 0,
    Event = 1,
};

enum class MarkerShape : int
{
    Lollipop = 0,
    Square = 1,
    Triangle = 2,
};

struct TimelineRule
{
    std::string name; // shown in tooltips; empty = the begin pattern
    TimelineKind kind = TimelineKind::Span;
    std::string begin; // Span: begins a bar; Event: the event line
    std::string end;   // Span only
    MatchType matchType = MatchType::Simple;
    MarkerShape shape = MarkerShape::Lollipop; // Event only
    Color color = Color::autoColor();          // Auto: a color per rule
    bool enabled = true;

    std::string displayName() const { return name.empty() ? begin : name; }
    bool operator==(const TimelineRule&) const = default;
};

const char* toText(TimelineKind kind);
const char* toText(MarkerShape shape);
std::optional<TimelineKind> parseTimelineKind(std::string_view text);
std::optional<MarkerShape> parseMarkerShape(std::string_view text);

void to_json(nlohmann::json& j, const TimelineRule& r);
void from_json(const nlohmann::json& j, TimelineRule& r);

// thrown when a rule cannot be compiled; identifies the rule
class TimelineError : public std::runtime_error
{
public:
    TimelineError(size_t ruleIndex, const std::string& message) :
        std::runtime_error(message),
        index(ruleIndex)
    {
    }

    size_t index;
};

struct TimelineSpan
{
    int64_t beginNs;
    int64_t endNs; // -1 while open
    uint64_t beginSeq;
    uint64_t endSeq; // UINT64_MAX while open
    uint32_t rule;
    uint32_t depth; // row within the lane, for overlapping spans
};

struct TimelineEvent
{
    int64_t ns;
    uint64_t seq;
    uint32_t rule;
};

// one row of the timeline per process, like the per-thread rows of TimeLens
struct TimelineLane
{
    std::string name;
    std::vector<TimelineSpan> spans;   // in begin order
    std::vector<TimelineEvent> events; // in time order
    std::vector<int64_t> rowEnds;      // per depth: end of its last span (INT64_MAX while open)
    int64_t maxDuration = 0;           // of the closed spans, to find spans that began before a visible range

    uint32_t depthCount() const { return rowEnds.empty() ? 1 : static_cast<uint32_t>(rowEnds.size()); }
};

// Turns lines into the lanes of a timeline. Fed incrementally with the lines of a view, in order.
class TimelineModel
{
public:
    explicit TimelineModel(std::vector<TimelineRule> rules = {}); // throws TimelineError

    void reset();
    void add(uint64_t seq, int64_t ns, std::string_view text, std::string_view process, uint32_t pid);

    const std::vector<TimelineRule>& rules() const { return m_rules; }
    const std::vector<TimelineLane>& lanes() const { return m_lanes; }
    bool empty() const { return m_firstNs > m_lastNs; }
    int64_t firstNs() const { return m_firstNs; }
    int64_t lastNs() const { return m_lastNs; } // the newest line, where open spans end for now

private:
    struct Compiled
    {
        std::optional<Matcher> begin;
        std::optional<Matcher> end;
    };

    TimelineLane& lane(std::string_view process);

    std::vector<TimelineRule> m_rules;
    std::vector<Compiled> m_compiled;
    FilterState m_state;
    std::vector<TimelineLane> m_lanes;
    std::map<std::string, size_t, std::less<>> m_laneIndex;
    // (rule, pid, capture groups) -> the open spans, innermost last, as (lane, span index)
    std::map<std::tuple<uint32_t, uint32_t, std::string>, std::vector<std::pair<size_t, size_t>>> m_open;
    std::string m_groups;
    int64_t m_firstNs = INT64_MAX;
    int64_t m_lastNs = INT64_MIN;
};

} // namespace dv
