#include "dvcore/Settings.h"

#include "dvcore/Platform.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace dv {

namespace {

constexpr std::array columnNames = {"bookmark", "line", "date", "time", "pid", "process", "message"};
constexpr const char* appKeys[] = {"window", "font", "linkViews", "alwaysOnTop", "hideWhenMinimized", "prefixPid", "historyLimit", "legacyCharset", "customColors", "views", "currentView", "sources"};

} // namespace

const char* columnName(Column column)
{
    auto i = static_cast<size_t>(column);
    return i < columnNames.size() ? columnNames[i] : "";
}

std::vector<ColumnSettings> ViewSettings::defaultColumns()
{
    // DebugView++ defaults: Date hidden; widths in pixels
    return {
        {true, 20, 0},   // bookmark
        {true, 60, 1},   // line
        {false, 90, 2},  // date
        {true, 90, 3},   // time
        {true, 60, 4},   // pid
        {true, 140, 5},  // process
        {true, 1500, 6}, // message
    };
}

AppSettings AppSettings::defaults()
{
    AppSettings s;
    ViewSettings view;
    view.name = "View 1";
    s.views.push_back(view);
#ifdef _WIN32
    auto& registry = SourceRegistry::instance();
    auto ods = registry.makeDefault("outputdebugstring");
    ods.name = "OutputDebugString";
    s.sources.push_back(ods);
#endif
    return s;
}

void to_json(nlohmann::json& j, const ColumnSettings& c)
{
    j = {{"visible", c.visible}, {"width", c.width}, {"order", c.order}};
}

void from_json(const nlohmann::json& j, ColumnSettings& c)
{
    c.visible = j.value("visible", c.visible);
    c.width = j.value("width", c.width);
    c.order = j.value("order", c.order);
}

void to_json(nlohmann::json& j, const ViewSettings& v)
{
    nlohmann::json columns = nlohmann::json::object();
    for (size_t i = 0; i < v.columns.size() && i < columnNames.size(); ++i)
        columns[columnNames[i]] = v.columns[i];
    j = {{"name", v.name}, {"filters", v.filters}, {"columns", columns}, {"clockTime", v.clockTime}, {"processColors", v.processColors}, {"autoScroll", v.autoScroll}, {"autoScrollStop", v.autoScrollStop}};
}

void from_json(const nlohmann::json& j, ViewSettings& v)
{
    v.name = j.value("name", v.name);
    if (j.contains("filters"))
        v.filters = j["filters"].get<FilterSet>();
    v.columns = ViewSettings::defaultColumns();
    if (auto it = j.find("columns"); it != j.end() && it->is_object())
    {
        for (size_t i = 0; i < columnNames.size(); ++i)
            if (it->contains(columnNames[i]))
                from_json((*it)[columnNames[i]], v.columns[i]);
    }
    v.clockTime = j.value("clockTime", v.clockTime);
    v.processColors = j.value("processColors", v.processColors);
    v.autoScroll = j.value("autoScroll", v.autoScroll);
    v.autoScrollStop = j.value("autoScrollStop", v.autoScrollStop);
}

void to_json(nlohmann::json& j, const AppSettings& s)
{
    j = s.unknownFields.is_object() ? s.unknownFields : nlohmann::json::object();
    j["window"] = {{"x", s.x}, {"y", s.y}, {"width", s.width}, {"height", s.height}, {"maximized", s.maximized}};
    j["font"] = {{"name", s.fontName}, {"size", s.fontSize}};
    j["linkViews"] = s.linkViews;
    j["alwaysOnTop"] = s.alwaysOnTop;
    j["hideWhenMinimized"] = s.hideWhenMinimized;
    j["prefixPid"] = s.prefixPid;
    j["historyLimit"] = s.historyLimit;
    j["legacyCharset"] = s.legacyCharset;
    j["customColors"] = s.customColors;
    j["views"] = s.views;
    j["currentView"] = s.currentView;
    nlohmann::json sources = nlohmann::json::array();
    for (auto& src : s.sources)
        if (!src.temporary)
            sources.push_back(src);
    j["sources"] = sources;
}

void from_json(const nlohmann::json& j, AppSettings& s)
{
    s = AppSettings{};
    if (auto w = j.find("window"); w != j.end() && w->is_object())
    {
        s.x = w->value("x", s.x);
        s.y = w->value("y", s.y);
        s.width = w->value("width", s.width);
        s.height = w->value("height", s.height);
        s.maximized = w->value("maximized", s.maximized);
    }
    if (auto f = j.find("font"); f != j.end() && f->is_object())
    {
        s.fontName = f->value("name", s.fontName);
        s.fontSize = f->value("size", s.fontSize);
    }
    s.linkViews = j.value("linkViews", s.linkViews);
    s.alwaysOnTop = j.value("alwaysOnTop", s.alwaysOnTop);
    s.hideWhenMinimized = j.value("hideWhenMinimized", s.hideWhenMinimized);
    s.prefixPid = j.value("prefixPid", s.prefixPid);
    s.historyLimit = j.value("historyLimit", s.historyLimit);
    s.legacyCharset = j.value("legacyCharset", s.legacyCharset);
    s.customColors = j.value("customColors", s.customColors);
    s.views = j.value("views", std::vector<ViewSettings>());
    s.currentView = j.value("currentView", 0);
    // sources of types this platform does not know are kept, so the settings survive a round trip
    s.sources = j.value("sources", std::vector<SourceConfig>());
    for (auto& [key, value] : j.items())
        if (std::find(std::begin(appKeys), std::end(appKeys), key) == std::end(appKeys))
            s.unknownFields[key] = value;
    if (s.views.empty())
        s.views.push_back(ViewSettings{});
    s.currentView = std::clamp(s.currentView, 0, static_cast<int>(s.views.size()) - 1);
}

std::filesystem::path defaultSettingsPath()
{
    return platform::configDirectory() / "settings.json";
}

AppSettings loadSettings(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return AppSettings::defaults();
    std::stringstream ss;
    ss << in.rdbuf();
    try
    {
        return nlohmann::json::parse(ss.str()).get<AppSettings>();
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("cannot read settings from '" + platform::pathToUtf8(path) + "': " + e.what());
    }
}

void saveSettings(const std::filesystem::path& path, const AppSettings& settings)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("cannot write '" + platform::pathToUtf8(temp) + "'");
        out << nlohmann::json(settings).dump(2) << '\n';
        if (!out)
            throw std::runtime_error("error writing '" + platform::pathToUtf8(temp) + "'");
    }
    std::filesystem::rename(temp, path, ec);
    if (ec)
        throw std::runtime_error("cannot replace '" + platform::pathToUtf8(path) + "': " + ec.message());
}

} // namespace dv
