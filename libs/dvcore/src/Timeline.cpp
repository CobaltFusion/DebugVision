#include "dvcore/Timeline.h"

#include <algorithm>
#include <array>
#include <utility>

namespace dv {

namespace {

constexpr std::array kindNames = {"Span", "Event"};
constexpr std::array shapeNames = {"Lollipop", "Square", "Triangle"};

template <typename E, size_t N>
std::optional<E> parse(const std::array<const char*, N>& names, std::string_view text)
{
    for (size_t i = 0; i < N; ++i)
        if (text == names[i])
            return static_cast<E>(i);
    return std::nullopt;
}

} // namespace

const char* toText(TimelineKind kind)
{
    auto i = static_cast<size_t>(kind);
    return i < kindNames.size() ? kindNames[i] : kindNames[0];
}

const char* toText(MarkerShape shape)
{
    auto i = static_cast<size_t>(shape);
    return i < shapeNames.size() ? shapeNames[i] : shapeNames[0];
}

std::optional<TimelineKind> parseTimelineKind(std::string_view text)
{
    return parse<TimelineKind>(kindNames, text);
}

std::optional<MarkerShape> parseMarkerShape(std::string_view text)
{
    return parse<MarkerShape>(shapeNames, text);
}

void to_json(nlohmann::json& j, const TimelineRule& r)
{
    j = {{"enabled", r.enabled}, {"name", r.name}, {"kind", toText(r.kind)}, {"begin", r.begin}, {"end", r.end},
        {"matchType", toText(r.matchType)}, {"shape", toText(r.shape)}, {"color", r.color}};
}

void from_json(const nlohmann::json& j, TimelineRule& r)
{
    r.enabled = j.value("enabled", true);
    r.name = j.value("name", std::string());
    r.kind = parseTimelineKind(j.value("kind", std::string("Span"))).value_or(TimelineKind::Span);
    r.begin = j.value("begin", std::string());
    r.end = j.value("end", std::string());
    r.matchType = parseMatchType(j.value("matchType", std::string("Simple"))).value_or(MatchType::Simple);
    r.shape = parseMarkerShape(j.value("shape", std::string("Lollipop"))).value_or(MarkerShape::Lollipop);
    if (j.contains("color"))
        from_json(j["color"], r.color);
}

TimelineModel::TimelineModel(std::vector<TimelineRule> rules) :
    m_rules(std::move(rules))
{
    m_compiled.resize(m_rules.size());
    for (size_t i = 0; i < m_rules.size(); ++i)
    {
        auto& rule = m_rules[i];
        if (!rule.enabled)
            continue;
        if (rule.begin.empty())
            throw TimelineError(i, "rule " + std::to_string(i + 1) + " has no begin pattern");
        if (rule.kind == TimelineKind::Span && rule.end.empty())
            throw TimelineError(i, "rule " + std::to_string(i + 1) + " ('" + rule.displayName() + "') is a span without an end pattern");
        try
        {
            m_compiled[i].begin.emplace(rule.begin, rule.matchType);
            if (rule.kind == TimelineKind::Span)
                m_compiled[i].end.emplace(rule.end, rule.matchType);
        }
        catch (const std::runtime_error& e)
        {
            throw TimelineError(i, e.what());
        }
    }
}

std::vector<std::pair<size_t, size_t>> TimelineModel::openSpans() const
{
    std::vector<std::pair<size_t, size_t>> result;
    for (auto& [key, spans] : m_open)
        result.insert(result.end(), spans.begin(), spans.end());
    return result;
}

void TimelineModel::reset()
{
    m_lanes.clear();
    m_laneIndex.clear();
    m_open.clear();
    m_firstNs = INT64_MAX;
    m_lastNs = INT64_MIN;
}

TimelineLane& TimelineModel::lane(std::string_view process)
{
    auto it = m_laneIndex.find(process);
    if (it == m_laneIndex.end())
    {
        it = m_laneIndex.emplace(std::string(process), m_lanes.size()).first;
        m_lanes.push_back(TimelineLane{std::string(process), {}, {}, {}, 0});
    }
    return m_lanes[it->second];
}

void TimelineModel::add(uint64_t seq, int64_t ns, std::string_view text, std::string_view process, uint32_t pid)
{
    m_firstNs = std::min(m_firstNs, ns);
    m_lastNs = std::max(m_lastNs, ns);
    for (size_t i = 0; i < m_rules.size(); ++i)
    {
        auto& compiled = m_compiled[i];
        if (!compiled.begin)
            continue;
        auto rule = static_cast<uint32_t>(i);
        if (m_rules[i].kind == TimelineKind::Event)
        {
            if (compiled.begin->matches(text, m_state))
                lane(process).events.push_back(TimelineEvent{ns, seq, rule});
            continue;
        }

        // an end closes the innermost open span with the same key; a line that also matches begin only begins a
        // span when there is nothing to close, so begin == end patterns toggle
        if (compiled.end->matchGroups(text, m_state, m_groups))
        {
            auto it = m_open.find(std::make_tuple(rule, pid, m_groups));
            if (it != m_open.end() && !it->second.empty())
            {
                auto [laneIndex, spanIndex] = it->second.back();
                it->second.pop_back();
                if (it->second.empty())
                    m_open.erase(it);
                auto& l = m_lanes[laneIndex];
                auto& span = l.spans[spanIndex];
                span.endNs = std::max(ns, span.beginNs);
                span.endSeq = seq;
                l.rowEnds[span.depth] = span.endNs;
                l.maxDuration = std::max(l.maxDuration, span.endNs - span.beginNs);
                continue;
            }
        }
        if (compiled.begin->matchGroups(text, m_state, m_groups))
        {
            auto& l = lane(process);
            uint32_t depth = 0;
            while (depth < l.rowEnds.size() && l.rowEnds[depth] > ns)
                ++depth;
            if (depth == l.rowEnds.size())
                l.rowEnds.push_back(INT64_MAX);
            l.rowEnds[depth] = INT64_MAX;
            auto laneIndex = static_cast<size_t>(&l - m_lanes.data());
            m_open[std::make_tuple(rule, pid, m_groups)].emplace_back(laneIndex, l.spans.size());
            l.spans.push_back(TimelineSpan{ns, -1, seq, UINT64_MAX, rule, depth});
        }
    }
}

} // namespace dv
