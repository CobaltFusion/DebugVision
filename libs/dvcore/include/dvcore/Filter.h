#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dv {

// The numeric values are the ones DebugView++ persisted; keep them stable.
enum class FilterType : int
{
    Include = 0,
    Exclude = 1,
    Highlight = 2,
    Token = 3,
    Stop = 4,
    Track = 5,
    Once = 6,
    Clear = 7,
    Beep = 8,
    MatchColor = 9, // legacy, converted to Token with an Auto background when loaded
    Bookmark = 10,
};

enum class MatchType : int
{
    Simple = 0,
    Wildcard = 1,
    Regex = 2,
    RegexGroups = 3,
    RegexCase = 4,
};

const char* toText(FilterType type);
const char* toText(MatchType type);
std::optional<FilterType> parseFilterType(std::string_view text);
std::optional<MatchType> parseMatchType(std::string_view text);

// 0x00RRGGBB, or Auto: a color is chosen per distinct matched text
struct Color
{
    uint32_t rgb = 0;
    bool isAuto = false;

    static Color fromRgb(uint8_t r, uint8_t g, uint8_t b) { return Color{(uint32_t(r) << 16) | (uint32_t(g) << 8) | b, false}; }
    static Color autoColor() { return Color{0, true}; }
    uint8_t red() const { return static_cast<uint8_t>(rgb >> 16); }
    uint8_t green() const { return static_cast<uint8_t>(rgb >> 8); }
    uint8_t blue() const { return static_cast<uint8_t>(rgb); }
    bool operator==(const Color&) const = default;
};

struct Filter
{
    std::string text;
    MatchType matchType = MatchType::Simple;
    FilterType filterType = FilterType::Include;
    Color background = Color::fromRgb(255, 255, 255);
    Color foreground = Color::fromRgb(0, 0, 0);
    bool enabled = true;
    bool operator==(const Filter&) const = default;
};

struct FilterSet
{
    std::string name;
    std::vector<Filter> messageFilters;
    std::vector<Filter> processFilters;
    bool operator==(const FilterSet&) const = default;
};

void to_json(nlohmann::json& j, const Color& c);
void from_json(const nlohmann::json& j, Color& c);
void to_json(nlohmann::json& j, const Filter& f);
void from_json(const nlohmann::json& j, Filter& f);
void to_json(nlohmann::json& j, const FilterSet& s);
void from_json(const nlohmann::json& j, FilterSet& s);

// Filter files. Our own format is JSON (see to_json above).
// Import also reads DebugView++ .xml filter files and DebugView++'s boost property-tree .json files.
// Throws std::runtime_error with a description on errors.
FilterSet loadFilterFile(const std::string& utf8Path);
FilterSet parseFilterFile(std::string_view content, bool xml);
void saveFilterFile(const std::string& utf8Path, const FilterSet& filters);

// random distinct colors along the golden-ratio hue sequence, as used by DebugView++
class ColorGenerator
{
public:
    ColorGenerator(double saturation, double value) :
        m_saturation(saturation),
        m_value(value)
    {
    }

    Color next();

private:
    double m_saturation;
    double m_value;
    double m_hue = 0.0;
};

} // namespace dv
