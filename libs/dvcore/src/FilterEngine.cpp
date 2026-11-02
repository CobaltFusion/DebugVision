#include "dvcore/FilterEngine.h"

#include "dvcore/Matcher.h"

#include <pcre2.h>

#include <algorithm>
#include <optional>

namespace dv {

namespace {

constexpr uint32_t ovectorPairs = 32;
constexpr size_t maxAutoStyles = 60000;

} // namespace

FilterState::FilterState() :
    m_matchData(pcre2_match_data_create_8(ovectorPairs, nullptr))
{
}

FilterState::~FilterState()
{
    if (m_matchData)
        pcre2_match_data_free_8(static_cast<pcre2_match_data_8*>(m_matchData));
}

FilterState::FilterState(FilterState&& other) noexcept :
    styles(std::move(other.styles)),
    autoStyles(std::move(other.autoStyles)),
    onceMatched(std::move(other.onceMatched)),
    backgroundColors(other.backgroundColors),
    textColors(other.textColors),
    m_matchData(std::exchange(other.m_matchData, nullptr))
{
}

FilterState& FilterState::operator=(FilterState&& other) noexcept
{
    if (this != &other)
    {
        if (m_matchData)
            pcre2_match_data_free_8(static_cast<pcre2_match_data_8*>(m_matchData));
        styles = std::move(other.styles);
        autoStyles = std::move(other.autoStyles);
        onceMatched = std::move(other.onceMatched);
        backgroundColors = other.backgroundColors;
        textColors = other.textColors;
        m_matchData = std::exchange(other.m_matchData, nullptr);
    }
    return *this;
}

void* FilterState::matchData()
{
    return m_matchData;
}

struct FilterEngine::Compiled
{
    Filter filter;
    size_t index = 0; // position in its list (for Once flags and error reporting)
    std::optional<Matcher> matcher;
    uint16_t style = 0;
    bool autoBackground = false;

    bool matches(std::string_view text, FilterState& state) const { return matcher->matches(text, state); }
    bool matchKey(std::string_view text, FilterState& state, std::string& key) const { return matcher->matchKey(text, state, key); }
};

struct FilterEngine::List
{
    std::vector<const Compiled*> enabled;
    std::vector<const Compiled*> excludes;
    std::vector<const Compiled*> includes; // Include and Once
    std::vector<const Compiled*> colorOrder; // Highlight first, then Include/Track/Stop/Once
    std::vector<const Compiled*> actions;    // Stop/Track/Beep/Bookmark
    std::vector<const Compiled*> clears;
    std::vector<const Compiled*> tokens;
    bool hasInclude = false;
};

FilterEngine::FilterEngine(const FilterSet& filters) :
    m_filters(filters),
    m_messageList(std::make_unique<List>()),
    m_processList(std::make_unique<List>())
{
    m_fixedStyles.push_back(RowStyle{}); // style 0 = default

    auto compile = [this](const std::vector<Filter>& list, bool process, std::vector<std::unique_ptr<Compiled>>& out, List& index) {
        for (size_t i = 0; i < list.size(); ++i)
        {
            auto c = std::make_unique<Compiled>();
            c->filter = list[i];
            c->index = i;
            auto& f = c->filter;
            if (f.filterType == FilterType::MatchColor)
            {
                f.filterType = FilterType::Token;
                f.background = Color::autoColor();
            }
            try
            {
                c->matcher.emplace(f.text, f.matchType);
            }
            catch (const std::runtime_error& e)
            {
                throw FilterError(process, i, e.what());
            }
            c->autoBackground = f.background.isAuto && !process;
            if (!c->autoBackground)
            {
                c->style = static_cast<uint16_t>(m_fixedStyles.size());
                m_fixedStyles.push_back(RowStyle{f.background, f.foreground, false});
            }
            out.push_back(std::move(c));
        }
        for (auto& c : out)
        {
            if (!c->filter.enabled)
                continue;
            auto type = c->filter.filterType;
            index.enabled.push_back(c.get());
            switch (type)
            {
            case FilterType::Exclude: index.excludes.push_back(c.get()); break;
            case FilterType::Include: index.includes.push_back(c.get()); index.hasInclude = true; break;
            case FilterType::Once: index.includes.push_back(c.get()); m_stateful = true; break;
            case FilterType::Clear: index.clears.push_back(c.get()); m_hasActions = true; break;
            case FilterType::Token: index.tokens.push_back(c.get()); break;
            case FilterType::Stop:
            case FilterType::Track:
            case FilterType::Beep:
            case FilterType::Bookmark: index.actions.push_back(c.get()); m_hasActions = true; break;
            case FilterType::Highlight:
            case FilterType::MatchColor: break;
            }
            if (type == FilterType::Highlight)
                index.colorOrder.push_back(c.get());
            if (c->autoBackground && (type == FilterType::Include || type == FilterType::Highlight || type == FilterType::Track || type == FilterType::Stop || type == FilterType::Once))
                m_stateful = true;
        }
        for (auto& c : out)
        {
            auto type = c->filter.filterType;
            if (c->filter.enabled && (type == FilterType::Include || type == FilterType::Track || type == FilterType::Stop || type == FilterType::Once))
                index.colorOrder.push_back(c.get());
        }
    };
    compile(m_filters.messageFilters, false, m_message, *m_messageList);
    compile(m_filters.processFilters, true, m_process, *m_processList);
}

FilterEngine::~FilterEngine() = default;

FilterState FilterEngine::makeState() const
{
    FilterState state;
    state.styles = m_fixedStyles;
    state.onceMatched.assign(m_message.size() + m_process.size(), false);
    return state;
}

bool FilterEngine::isIncluded(const List& list, std::string_view text, FilterState& state, size_t onceOffset) const
{
    for (auto f : list.excludes)
        if (f->matches(text, state))
            return false;
    bool included = false;
    for (auto f : list.includes)
    {
        if (f->filter.filterType == FilterType::Once)
        {
            auto flag = onceOffset + f->index;
            if (flag < state.onceMatched.size() && !state.onceMatched[flag] && f->matches(text, state))
            {
                state.onceMatched[flag] = true;
                included = true;
            }
        }
        else if (!included && f->matches(text, state))
        {
            included = true;
        }
    }
    // DebugView++ semantics: without an Include filter everything that is not excluded is shown
    return !list.hasInclude || included;
}

uint16_t FilterEngine::autoStyle(const Compiled& f, std::string_view text, FilterState& state) const
{
    std::string key;
    if (!f.matchKey(text, state, key))
        return 0;
    auto it = state.autoStyles.find(key);
    if (it != state.autoStyles.end())
        return it->second;
    if (state.styles.size() >= maxAutoStyles)
        return f.style;
    auto id = static_cast<uint16_t>(state.styles.size());
    state.styles.push_back(RowStyle{state.backgroundColors.next(), f.filter.foreground, false});
    state.autoStyles.emplace(std::move(key), id);
    return id;
}

uint16_t FilterEngine::rowStyle(const List& list, std::string_view text, FilterState& state) const
{
    for (auto f : list.colorOrder)
    {
        if (!f->matches(text, state))
            continue;
        return f->autoBackground ? autoStyle(*f, text, state) : f->style;
    }
    return 0;
}

FilterEngine::Result FilterEngine::evaluate(std::string_view message, std::string_view process, FilterState& state) const
{
    Result result;
    for (auto f : m_messageList->clears)
    {
        if (f->matches(message, state))
            result.actions |= ActionClear;
    }
    result.included = isIncluded(*m_processList, process, state, m_message.size()) && isIncluded(*m_messageList, message, state, 0);
    if (!result.included)
        return result;

    auto style = rowStyle(*m_messageList, message, state);
    if (style == 0)
        style = rowStyle(*m_processList, process, state);
    result.style = style;

    auto addActions = [&](const List& list, std::string_view text) {
        for (auto f : list.actions)
        {
            if (!f->matches(text, state))
                continue;
            switch (f->filter.filterType)
            {
            case FilterType::Stop: result.actions |= ActionStop; break;
            case FilterType::Track: result.actions |= ActionTrack; break;
            case FilterType::Beep: result.actions |= ActionBeep; break;
            case FilterType::Bookmark: result.actions |= ActionBookmark; break;
            case FilterType::Include:
            case FilterType::Exclude:
            case FilterType::Highlight:
            case FilterType::Token:
            case FilterType::Once:
            case FilterType::Clear:
            case FilterType::MatchColor: break;
            }
        }
    };
    addActions(*m_messageList, message);
    addActions(*m_processList, process);
    return result;
}

std::vector<HighlightSpan> FilterEngine::highlights(std::string_view message, FilterState& state) const
{
    std::vector<HighlightSpan> spans;
    for (auto f : m_messageList->tokens)
    {
        f->matcher->forEachMatch(message, state, [&](size_t begin, size_t end, const std::string& key) {
            if (end <= begin)
                return;
            Color bg = f->filter.background;
            if (f->autoBackground)
            {
                auto it = state.autoStyles.find(key);
                if (it == state.autoStyles.end())
                {
                    auto color = state.backgroundColors.next();
                    auto id = static_cast<uint16_t>(state.styles.size());
                    state.styles.push_back(RowStyle{color, f->filter.foreground, false});
                    it = state.autoStyles.emplace(key, id).first;
                }
                bg = state.styles[it->second].background;
            }
            spans.push_back(HighlightSpan{begin, end, bg, f->filter.foreground});
        });
    }
    return spans;
}

} // namespace dv
