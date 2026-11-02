#pragma once

#include "dvcore/Filter.h"
#include "dvcore/SourceConfig.h"
#include "dvcore/Timeline.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace dv {

enum class Column : int
{
    Bookmark = 0,
    Line,
    Date,
    Time,
    Pid,
    Process,
    Message,
    Count,
};

const char* columnName(Column column);

struct ColumnSettings
{
    bool visible = true;
    int width = 100;
    int order = 0;
};

struct ViewSettings
{
    std::string name = "View";
    FilterSet filters;
    std::vector<ColumnSettings> columns = defaultColumns();
    bool clockTime = true;
    bool processColors = false;
    bool autoScroll = true;
    bool autoScrollStop = true;
    std::vector<TimelineRule> timelineRules;
    bool showTimeline = false;
    int timelineHeight = 200; // pixels

    static std::vector<ColumnSettings> defaultColumns();
};

struct AppSettings
{
    int x = -1;
    int y = -1;
    int width = 1100;
    int height = 700;
    bool maximized = false;
    std::string fontName;
    int fontSize = 9;
    bool linkViews = false;
    bool alwaysOnTop = false;
    bool hideWhenMinimized = false;
    bool prefixPid = false;
    uint64_t historyLimit = 0; // lines, 0 = unlimited
    std::string legacyCharset; // default charset for 8-bit text that is not UTF-8
    std::vector<uint32_t> customColors;
    std::vector<ViewSettings> views;
    int currentView = 0;
    std::vector<SourceConfig> sources;
    nlohmann::json unknownFields = nlohmann::json::object();

    // defaults for the first start: one view, and the OutputDebugString source on Windows
    static AppSettings defaults();
};

void to_json(nlohmann::json& j, const ColumnSettings& c);
void from_json(const nlohmann::json& j, ColumnSettings& c);
void to_json(nlohmann::json& j, const ViewSettings& v);
void from_json(const nlohmann::json& j, ViewSettings& v);
void to_json(nlohmann::json& j, const AppSettings& s);
void from_json(const nlohmann::json& j, AppSettings& s);

std::filesystem::path defaultSettingsPath();
// returns defaults() when the file does not exist; throws std::runtime_error when it cannot be parsed
AppSettings loadSettings(const std::filesystem::path& path);
// writes to a temporary file and renames it, so a crash never leaves a half written file; throws std::runtime_error
void saveSettings(const std::filesystem::path& path, const AppSettings& settings);

} // namespace dv
