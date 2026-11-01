#include "dvcore/FilterEngine.h"

#include "dvcore/Encoding.h"

#include <pcre2.h>

#include <algorithm>
#include <cstring>

namespace dv {

namespace {

constexpr uint32_t ovectorPairs = 32;
constexpr size_t maxAutoStyles = 60000;

char toLowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string toLowerAscii(std::string_view s)
{
    std::string r(s);
    for (auto& c : r)
        c = toLowerAscii(c);
    return r;
}

std::string escapeRegex(std::string_view text)
{
    std::string out;
    for (char c : text)
    {
        if (std::strchr("\\^$.|?*+()[]{}", c) && c != '\0')
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// DebugView++ compatible: '*' -> '.*', '?' -> '.?', not anchored
std::string wildcardToRegex(std::string_view text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '*')
            out += ".*";
        else if (c == '?')
            out += ".?";
        else
            out += escapeRegex(std::string_view(&c, 1));
    }
    return out;
}

} // namespace

size_t findIgnoreCase(std::string_view haystack, std::string_view lowerNeedle, size_t from)
{
    if (lowerNeedle.empty())
        return from <= haystack.size() ? from : std::string_view::npos;
    if (haystack.size() < lowerNeedle.size())
        return std::string_view::npos;
    auto first = lowerNeedle[0];
    auto firstUpper = (first >= 'a' && first <= 'z') ? static_cast<char>(first - 'a' + 'A') : first;
    auto last = haystack.size() - lowerNeedle.size();
    for (auto i = from; i <= last; ++i)
    {
        // scan for the first character with memchr when it has no case variant
        if (first == firstUpper)
        {
            auto p = static_cast<const char*>(std::memchr(haystack.data() + i, first, last - i + 1));
            if (!p)
                return std::string_view::npos;
            i = static_cast<size_t>(p - haystack.data());
        }
        else if (haystack[i] != first && haystack[i] != firstUpper)
        {
            continue;
        }
        size_t k = 1;
        while (k < lowerNeedle.size() && toLowerAscii(haystack[i + k]) == lowerNeedle[k])
            ++k;
        if (k == lowerNeedle.size())
            return i;
    }
    return std::string_view::npos;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view lowerNeedle)
{
    return findIgnoreCase(haystack, lowerNeedle) != std::string_view::npos;
}

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
    bool literal = false;
    std::string lowerNeedle;
    pcre2_code_8* code = nullptr;
    uint16_t style = 0;
    bool autoBackground = false;

    ~Compiled()
    {
        if (code)
            pcre2_code_free_8(code);
    }

    bool matches(std::string_view text, FilterState& state) const
    {
        if (literal)
            return containsIgnoreCase(text, lowerNeedle);
        auto md = static_cast<pcre2_match_data_8*>(state.matchData());
        return pcre2_match_8(code, reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), 0, PCRE2_NO_UTF_CHECK, md, nullptr) >= 0;
    }

    // calls f(begin, end, key) for every match (or every capture group with RegexGroups)
    template <typename F>
    void forEachMatch(std::string_view text, FilterState& state, F&& f) const
    {
        if (literal)
        {
            if (lowerNeedle.empty())
                return;
            for (auto pos = findIgnoreCase(text, lowerNeedle); pos != std::string_view::npos; pos = findIgnoreCase(text, lowerNeedle, pos + lowerNeedle.size()))
                f(pos, pos + lowerNeedle.size(), toLowerAscii(text.substr(pos, lowerNeedle.size())));
            return;
        }
        auto md = static_cast<pcre2_match_data_8*>(state.matchData());
        size_t offset = 0;
        while (offset <= text.size())
        {
            auto rc = pcre2_match_8(code, reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), offset, PCRE2_NO_UTF_CHECK, md, nullptr);
            if (rc < 0)
                return;
            auto ov = pcre2_get_ovector_pointer_8(md);
            if (filter.matchType == MatchType::RegexGroups && rc > 1)
            {
                for (int g = 1; g < rc; ++g)
                {
                    if (ov[2 * g] == PCRE2_UNSET)
                        continue;
                    f(ov[2 * g], ov[2 * g + 1], toLowerAscii(text.substr(ov[2 * g], ov[2 * g + 1] - ov[2 * g])));
                }
            }
            else
            {
                f(ov[0], ov[1], toLowerAscii(text.substr(ov[0], ov[1] - ov[0])));
            }
            auto next = ov[1] > ov[0] ? ov[1] : ov[0] + 1;
            // do not continue inside a UTF-8 sequence after an empty match
            while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
                ++next;
            offset = next;
        }
    }

    // the key that selects an Auto color: the lower-cased match, or the capture groups joined with '\0'
    bool matchKey(std::string_view text, FilterState& state, std::string& key) const
    {
        if (literal)
        {
            auto pos = findIgnoreCase(text, lowerNeedle);
            if (pos == std::string_view::npos)
                return false;
            key = toLowerAscii(text.substr(pos, lowerNeedle.size()));
            return true;
        }
        auto md = static_cast<pcre2_match_data_8*>(state.matchData());
        auto rc = pcre2_match_8(code, reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), 0, PCRE2_NO_UTF_CHECK, md, nullptr);
        if (rc < 0)
            return false;
        auto ov = pcre2_get_ovector_pointer_8(md);
        key.clear();
        if (filter.matchType == MatchType::RegexGroups && rc > 1)
        {
            for (int g = 1; g < rc; ++g)
            {
                if (g > 1)
                    key.push_back('\0');
                if (ov[2 * g] != PCRE2_UNSET)
                    key += toLowerAscii(text.substr(ov[2 * g], ov[2 * g + 1] - ov[2 * g]));
            }
        }
        else
        {
            key = toLowerAscii(text.substr(ov[0], ov[1] - ov[0]));
        }
        return true;
    }
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
            bool plainText = f.matchType == MatchType::Simple || (f.matchType == MatchType::Wildcard && f.text.find_first_of("*?") == std::string::npos);
            if (plainText && isAscii(f.text))
            {
                c->literal = true;
                c->lowerNeedle = toLowerAscii(f.text);
            }
            else
            {
                std::string pattern = f.matchType == MatchType::Simple ? escapeRegex(f.text) : f.matchType == MatchType::Wildcard ? wildcardToRegex(f.text) : f.text;
                uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
                if (f.matchType != MatchType::RegexCase)
                    options |= PCRE2_CASELESS;
                int errorCode = 0;
                PCRE2_SIZE errorOffset = 0;
                c->code = pcre2_compile_8(reinterpret_cast<PCRE2_SPTR8>(pattern.data()), pattern.size(), options, &errorCode, &errorOffset, nullptr);
                if (!c->code)
                {
                    PCRE2_UCHAR8 buffer[256];
                    pcre2_get_error_message_8(errorCode, buffer, sizeof(buffer));
                    throw FilterError(process, i, "error in filter '" + f.text + "' at offset " + std::to_string(errorOffset) + ": " + reinterpret_cast<char*>(buffer));
                }
                pcre2_jit_compile_8(c->code, PCRE2_JIT_COMPLETE); // falls back to the interpreter when JIT is unavailable
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
        f->forEachMatch(message, state, [&](size_t begin, size_t end, const std::string& key) {
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
