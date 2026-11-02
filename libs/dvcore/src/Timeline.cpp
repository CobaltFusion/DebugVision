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

// index of the ']' that closes the character class starting at p[open] == '['
size_t endOfClass(std::string_view p, size_t open)
{
    auto j = open + 1;
    if (j < p.size() && p[j] == '^')
        ++j;
    if (j < p.size() && p[j] == ']')
        ++j; // a ']' first in the class is literal
    for (; j < p.size(); ++j)
    {
        if (p[j] == '\\')
            ++j;
        else if (p[j] == '[' && j + 1 < p.size() && p[j + 1] == ':')
        {
            if (auto e = p.find(":]", j + 2); e != std::string_view::npos)
                j = e + 1; // [:alpha:]
        }
        else if (p[j] == ']')
            return j;
    }
    return p.size();
}

// calls f(i) for every position of a regex outside escapes, \Q...\E and character classes, and lets f(i) skip
// ahead by returning the last position it consumed
template <typename F>
void scanPattern(std::string_view p, F&& f)
{
    for (size_t i = 0; i < p.size(); ++i)
    {
        if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == 'Q')
        {
            auto e = p.find("\\E", i + 2);
            if (e == std::string_view::npos)
                return;
            i = e + 1;
        }
        else if (p[i] == '[')
        {
            i = endOfClass(p, i);
        }
        else
        {
            i = f(i);
        }
    }
}

struct CaptureGroup
{
    size_t open;       // position of '('
    size_t innerBegin; // first position of the group's own pattern
    size_t close;      // position of ')'
};

// the capturing groups of a PCRE2 pattern, in the order they are numbered
std::vector<CaptureGroup> captureGroups(std::string_view p)
{
    std::vector<CaptureGroup> groups;
    std::vector<ptrdiff_t> open; // per open parenthesis: index in groups, or -1 when it does not capture
    scanPattern(p, [&](size_t i) {
        if (p[i] == '\\')
            return i + 1;
        if (p[i] == '(')
        {
            auto rest = p.substr(i + 1);
            size_t inner = i + 1;
            bool capturing = true;
            if (rest.starts_with("?") || rest.starts_with("*"))
            {
                // named groups capture: (?<name>...), (?'name'...), (?P<name>...); lookbehinds and the rest do not
                capturing = rest.starts_with("?P<") || rest.starts_with("?'") || (rest.starts_with("?<") && !rest.starts_with("?<=") && !rest.starts_with("?<!"));
                if (capturing)
                    inner = p.find_first_of(">'", i + 3) + 1;
            }
            open.push_back(capturing ? static_cast<ptrdiff_t>(groups.size()) : -1);
            if (capturing)
                groups.push_back(CaptureGroup{i, inner, std::string_view::npos});
        }
        else if (p[i] == ')' && !open.empty())
        {
            if (open.back() >= 0)
                groups[static_cast<size_t>(open.back())].close = i;
            open.pop_back();
        }
        return i;
    });
    return groups;
}

std::string joinKey(const std::vector<std::string>& groups, const std::vector<size_t>& indices, bool caseless)
{
    std::string key;
    for (size_t k = 0; k < indices.size(); ++k)
    {
        if (k > 0)
            key.push_back('\0');
        if (indices[k] < groups.size())
            key += groups[indices[k]];
    }
    if (caseless)
        for (auto& c : key)
            c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    return key;
}

// the text a span shows: the given groups (all groups when empty) as captured, in pattern order, separated by spaces
std::string spanLabel(const std::vector<std::string>& groups, const std::vector<size_t>& indices)
{
    std::string label;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        if (groups[g].empty() || (!indices.empty() && std::find(indices.begin(), indices.end(), g) == indices.end()))
            continue;
        if (!label.empty())
            label.push_back(' ');
        label += groups[g];
    }
    return label;
}

} // namespace

std::string TimelineModel::rewriteBackReferences(const std::string& begin, const std::string& end, std::vector<size_t>& beginGroups,
    std::vector<size_t>& endGroups)
{
    auto groups = captureGroups(begin);
    std::string out;
    size_t copied = 0;
    std::vector<size_t> inserted; // positions of the '(' replacing a reference
    std::string error;
    scanPattern(end, [&](size_t i) {
        if (end[i] != '\\' || i + 1 >= end.size())
            return end[i] == '\\' ? i + 1 : i;
        auto c = end[i + 1];
        if (c < '1' || c > '9')
            return i + 1;
        if (i + 2 < end.size() && end[i + 2] >= '0' && end[i + 2] <= '9')
        {
            error = "only \\1 to \\9 can refer to the groups of the begin pattern";
            return end.size();
        }
        auto n = static_cast<size_t>(c - '0');
        if (n > groups.size() || groups[n - 1].close == std::string_view::npos)
        {
            error = "the end pattern refers to \\" + std::to_string(n) + ", but the begin pattern has " + std::to_string(groups.size()) + " capture group(s)";
            return end.size();
        }
        auto& g = groups[n - 1];
        out.append(end, copied, i - copied);
        inserted.push_back(out.size());
        out += "(" + begin.substr(g.innerBegin, g.close - g.innerBegin) + ")";
        beginGroups.push_back(n - 1);
        copied = i + 2;
        return i + 1;
    });
    if (!error.empty())
        throw std::runtime_error(error);
    if (inserted.empty())
        return std::string();
    out.append(end, copied);

    // the groups that replaced the references, by their number in the rewritten pattern
    auto rewritten = captureGroups(out);
    for (auto position : inserted)
        for (size_t k = 0; k < rewritten.size(); ++k)
            if (rewritten[k].open == position)
                endGroups.push_back(k);
    return out;
}

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
        auto& compiled = m_compiled[i];
        compiled.caseless = rule.matchType != MatchType::RegexCase;
        try
        {
            compiled.begin.emplace(rule.begin, rule.matchType);
            if (rule.kind == TimelineKind::Span)
            {
                // \1..\9 in the end pattern refer to the groups of the begin pattern (regular expressions only)
                bool regex = rule.matchType == MatchType::Regex || rule.matchType == MatchType::RegexGroups || rule.matchType == MatchType::RegexCase;
                auto rewritten = regex ? rewriteBackReferences(rule.begin, rule.end, compiled.beginKeyGroups, compiled.endKeyGroups) : std::string();
                compiled.end.emplace(rewritten.empty() ? rule.end : rewritten, rule.matchType);
            }
        }
        catch (const std::runtime_error& e)
        {
            throw TimelineError(i, "rule " + std::to_string(i + 1) + " ('" + rule.displayName() + "'): " + e.what());
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

        // the key that pairs a begin with an end: all capture groups of both patterns, or with back references
        // in the end pattern the referenced groups of begin and the groups that replaced the references in end
        auto key = [&](const Matcher& matcher, const std::vector<size_t>& keyGroups) -> std::optional<std::string> {
            if (!matcher.matchGroups(text, m_state, m_groups))
                return std::nullopt;
            if (!keyGroups.empty())
                return joinKey(m_groups, keyGroups, compiled.caseless);
            std::vector<size_t> all(m_groups.size());
            for (size_t g = 0; g < all.size(); ++g)
                all[g] = g;
            return joinKey(m_groups, all, compiled.caseless);
        };

        // an end closes the innermost open span with the same key; a line that also matches begin only begins a
        // span when there is nothing to close, so begin == end patterns toggle
        if (auto endKey = key(*compiled.end, compiled.endKeyGroups))
        {
            auto it = m_open.find(std::make_tuple(rule, pid, *endKey));
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
        if (auto beginKey = key(*compiled.begin, compiled.beginKeyGroups))
        {
            auto& l = lane(process);
            uint32_t depth = 0;
            while (depth < l.rowEnds.size() && l.rowEnds[depth] > ns)
                ++depth;
            if (depth == l.rowEnds.size())
                l.rowEnds.push_back(INT64_MAX);
            l.rowEnds[depth] = INT64_MAX;
            auto laneIndex = static_cast<size_t>(&l - m_lanes.data());
            m_open[std::make_tuple(rule, pid, *beginKey)].emplace_back(laneIndex, l.spans.size());
            l.spans.push_back(TimelineSpan{ns, -1, seq, UINT64_MAX, rule, depth, spanLabel(m_groups, compiled.beginKeyGroups)});
        }
    }
}

} // namespace dv
