#pragma once

#include "dvcore/Filter.h"

#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dv {

// thrown when a filter cannot be compiled; identifies the offending row
class FilterError : public std::runtime_error
{
public:
    FilterError(bool isProcessFilter, size_t filterIndex, const std::string& message) :
        std::runtime_error(message),
        processFilter(isProcessFilter),
        index(filterIndex)
    {
    }

    bool processFilter;
    size_t index;
};

enum LineAction : uint8_t
{
    ActionNone = 0,
    ActionStop = 1,
    ActionTrack = 2,
    ActionBeep = 4,
    ActionClear = 8,
    ActionBookmark = 16,
};

// resolved foreground/background of a row; style 0 is the default style
struct RowStyle
{
    Color background;
    Color foreground;
    bool isDefault = true;
};

struct HighlightSpan
{
    size_t begin; // byte offsets into the UTF-8 message
    size_t end;
    Color background;
    Color foreground;
};

class CompiledMatcher;

// Per thread / per view mutable state: PCRE2 match data, Once flags and Auto colors.
class FilterState
{
public:
    FilterState();
    ~FilterState();
    FilterState(FilterState&&) noexcept;
    FilterState& operator=(FilterState&&) noexcept;

    std::vector<RowStyle> styles; // index = style id, shared with the painting code
    std::map<std::string, uint16_t> autoStyles; // match key -> style id
    std::vector<bool> onceMatched;              // per message filter, then per process filter
    ColorGenerator backgroundColors{0.5, 0.95};
    ColorGenerator textColors{0.9, 0.7};

    void* matchData(); // pcre2_match_data_8*

private:
    void* m_matchData = nullptr;
};

// A compiled filter set. Immutable after construction, safe to share between threads.
class FilterEngine
{
public:
    explicit FilterEngine(const FilterSet& filters); // throws FilterError
    ~FilterEngine();

    struct Result
    {
        bool included = true;
        uint16_t style = 0;
        uint8_t actions = ActionNone;
    };

    Result evaluate(std::string_view message, std::string_view process, FilterState& state) const;

    // a new state with the fixed styles of this engine
    FilterState makeState() const;

    // true when evaluation depends on earlier lines (Once filters, Auto colors), which prevents parallel re-filtering
    bool isStateful() const { return m_stateful; }
    bool hasActions() const { return m_hasActions; }
    bool isEmpty() const { return m_message.empty() && m_process.empty(); }

    // Token highlighting of a message (only computed for painted rows)
    std::vector<HighlightSpan> highlights(std::string_view message, FilterState& state) const;

    const FilterSet& filters() const { return m_filters; }

private:
    struct Compiled;
    struct List;

    bool isIncluded(const List& list, std::string_view text, FilterState& state, size_t onceOffset) const;
    uint16_t rowStyle(const List& list, std::string_view text, FilterState& state) const;
    uint16_t autoStyle(const Compiled& f, std::string_view text, FilterState& state) const;

    FilterSet m_filters;
    std::vector<std::unique_ptr<Compiled>> m_message;
    std::vector<std::unique_ptr<Compiled>> m_process;
    std::unique_ptr<List> m_messageList;
    std::unique_ptr<List> m_processList;
    std::vector<RowStyle> m_fixedStyles;
    bool m_stateful = false;
    bool m_hasActions = false;
};

// case-insensitive (ASCII) substring search
bool containsIgnoreCase(std::string_view haystack, std::string_view lowerNeedle);
size_t findIgnoreCase(std::string_view haystack, std::string_view lowerNeedle, size_t from = 0);

} // namespace dv
