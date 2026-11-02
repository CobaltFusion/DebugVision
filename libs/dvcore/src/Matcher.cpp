#include "dvcore/Matcher.h"

#include "dvcore/Encoding.h"

#include <pcre2.h>

#include <cstring>
#include <stdexcept>
#include <utility>

namespace dv {

namespace {

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

pcre2_code_8* code(void* p)
{
    return static_cast<pcre2_code_8*>(p);
}

pcre2_match_data_8* matchData(FilterState& state)
{
    return static_cast<pcre2_match_data_8*>(state.matchData());
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

Matcher::Matcher(const std::string& text, MatchType type) :
    m_type(type)
{
    bool plainText = type == MatchType::Simple || (type == MatchType::Wildcard && text.find_first_of("*?") == std::string::npos);
    if (plainText && isAscii(text))
    {
        m_literal = true;
        m_lowerNeedle = toLowerAscii(text);
        return;
    }
    std::string pattern = type == MatchType::Simple ? escapeRegex(text) : type == MatchType::Wildcard ? wildcardToRegex(text) : text;
    uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
    if (type != MatchType::RegexCase)
        options |= PCRE2_CASELESS;
    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    m_code = pcre2_compile_8(reinterpret_cast<PCRE2_SPTR8>(pattern.data()), pattern.size(), options, &errorCode, &errorOffset, nullptr);
    if (!m_code)
    {
        PCRE2_UCHAR8 buffer[256];
        pcre2_get_error_message_8(errorCode, buffer, sizeof(buffer));
        throw std::runtime_error("error in '" + text + "' at offset " + std::to_string(errorOffset) + ": " + reinterpret_cast<char*>(buffer));
    }
    pcre2_jit_compile_8(code(m_code), PCRE2_JIT_COMPLETE); // falls back to the interpreter when JIT is unavailable
}

Matcher::~Matcher()
{
    if (m_code)
        pcre2_code_free_8(code(m_code));
}

Matcher::Matcher(Matcher&& other) noexcept :
    m_type(other.m_type),
    m_literal(other.m_literal),
    m_lowerNeedle(std::move(other.m_lowerNeedle)),
    m_code(std::exchange(other.m_code, nullptr))
{
}

Matcher& Matcher::operator=(Matcher&& other) noexcept
{
    if (this != &other)
    {
        if (m_code)
            pcre2_code_free_8(code(m_code));
        m_type = other.m_type;
        m_literal = other.m_literal;
        m_lowerNeedle = std::move(other.m_lowerNeedle);
        m_code = std::exchange(other.m_code, nullptr);
    }
    return *this;
}

bool Matcher::matches(std::string_view text, FilterState& state) const
{
    if (m_literal)
        return containsIgnoreCase(text, m_lowerNeedle);
    return pcre2_match_8(code(m_code), reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), 0, PCRE2_NO_UTF_CHECK, matchData(state), nullptr) >= 0;
}

void Matcher::forEachMatch(std::string_view text, FilterState& state, const std::function<void(size_t, size_t, const std::string&)>& f) const
{
    if (m_literal)
    {
        if (m_lowerNeedle.empty())
            return;
        for (auto pos = findIgnoreCase(text, m_lowerNeedle); pos != std::string_view::npos; pos = findIgnoreCase(text, m_lowerNeedle, pos + m_lowerNeedle.size()))
            f(pos, pos + m_lowerNeedle.size(), toLowerAscii(text.substr(pos, m_lowerNeedle.size())));
        return;
    }
    auto md = matchData(state);
    size_t offset = 0;
    while (offset <= text.size())
    {
        auto rc = pcre2_match_8(code(m_code), reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), offset, PCRE2_NO_UTF_CHECK, md, nullptr);
        if (rc < 0)
            return;
        auto ov = pcre2_get_ovector_pointer_8(md);
        if (m_type == MatchType::RegexGroups && rc > 1)
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

bool Matcher::matchKey(std::string_view text, FilterState& state, std::string& key) const
{
    if (m_literal)
    {
        auto pos = findIgnoreCase(text, m_lowerNeedle);
        if (pos == std::string_view::npos)
            return false;
        key = toLowerAscii(text.substr(pos, m_lowerNeedle.size()));
        return true;
    }
    auto md = matchData(state);
    auto rc = pcre2_match_8(code(m_code), reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), 0, PCRE2_NO_UTF_CHECK, md, nullptr);
    if (rc < 0)
        return false;
    auto ov = pcre2_get_ovector_pointer_8(md);
    key.clear();
    if (m_type == MatchType::RegexGroups && rc > 1)
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

bool Matcher::matchGroups(std::string_view text, FilterState& state, std::string& groups) const
{
    groups.clear();
    if (m_literal)
        return containsIgnoreCase(text, m_lowerNeedle);
    auto md = matchData(state);
    auto rc = pcre2_match_8(code(m_code), reinterpret_cast<PCRE2_SPTR8>(text.data()), text.size(), 0, PCRE2_NO_UTF_CHECK, md, nullptr);
    if (rc < 0)
        return false;
    auto ov = pcre2_get_ovector_pointer_8(md);
    for (int g = 1; g < rc; ++g)
    {
        if (g > 1)
            groups.push_back('\0');
        if (ov[2 * g] != PCRE2_UNSET)
            groups.append(text.substr(ov[2 * g], ov[2 * g + 1] - ov[2 * g]));
    }
    return true;
}

} // namespace dv
