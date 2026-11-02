#pragma once

#include "dvcore/Filter.h"
#include "dvcore/FilterEngine.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace dv {

// One compiled text pattern, as used by filters: Simple (plain text) and Wildcard are case-insensitive, Regex and
// RegexGroups are case-insensitive PCRE2, RegexCase is case-sensitive. Plain ASCII text uses a literal search
// instead of PCRE2. Immutable and safe to share between threads; matching uses the match data of a FilterState.
class Matcher
{
public:
    Matcher(const std::string& text, MatchType type); // throws std::runtime_error for an invalid pattern
    ~Matcher();
    Matcher(Matcher&& other) noexcept;
    Matcher& operator=(Matcher&& other) noexcept;
    Matcher(const Matcher&) = delete;
    Matcher& operator=(const Matcher&) = delete;

    bool matches(std::string_view text, FilterState& state) const;

    // the key that selects an Auto color: the lower-cased match, or with RegexGroups the capture groups joined with '\0'
    bool matchKey(std::string_view text, FilterState& state, std::string& key) const;

    // calls f(begin, end, key) for every match (or every capture group with RegexGroups); key is lower-cased
    void forEachMatch(std::string_view text, FilterState& state, const std::function<void(size_t, size_t, const std::string&)>& f) const;

    // like matches(), and returns the capture groups of the match joined with '\0' (empty when the pattern has none)
    bool matchGroups(std::string_view text, FilterState& state, std::string& groups) const;
    // the same, one string per capture group (empty when a group did not participate)
    bool matchGroups(std::string_view text, FilterState& state, std::vector<std::string>& groups) const;

private:
    MatchType m_type;
    bool m_literal = false;
    std::string m_lowerNeedle;
    void* m_code = nullptr; // pcre2_code_8*
};

} // namespace dv
