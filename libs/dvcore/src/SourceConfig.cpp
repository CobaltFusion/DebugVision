#include "dvcore/SourceConfig.h"

#include "dvcore/Platform.h"
#include "dvcore/Source.h"
#include "sources/Sources.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace dv {

namespace {

template <typename T>
void read(const nlohmann::json& j, const char* key, T& value)
{
    auto it = j.find(key);
    if (it != j.end() && !it->is_null())
        it->get_to(value);
}

std::string trim(std::string s)
{
    auto b = s.find_first_not_of(" \t");
    auto e = s.find_last_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

} // namespace

void to_json(nlohmann::json& j, const OutputDebugStringParams& p)
{
    j = {{"scope", p.scope}};
}

void from_json(const nlohmann::json& j, OutputDebugStringParams& p)
{
    read(j, "scope", p.scope);
}

void to_json(nlohmann::json& j, const UdpParams& p)
{
    j = {{"bindAddress", p.bindAddress}, {"port", p.port}, {"multicastGroup", p.multicastGroup}, {"allowedSenders", p.allowedSenders}, {"processName", p.processName}, {"splitOnNewline", p.splitOnNewline}};
}

void from_json(const nlohmann::json& j, UdpParams& p)
{
    read(j, "bindAddress", p.bindAddress);
    read(j, "port", p.port);
    read(j, "multicastGroup", p.multicastGroup);
    read(j, "allowedSenders", p.allowedSenders);
    read(j, "processName", p.processName);
    read(j, "splitOnNewline", p.splitOnNewline);
}

void to_json(nlohmann::json& j, const TcpListenParams& p)
{
    j = {{"bindAddress", p.bindAddress}, {"port", p.port}, {"maxClients", p.maxClients}};
}

void from_json(const nlohmann::json& j, TcpListenParams& p)
{
    read(j, "bindAddress", p.bindAddress);
    read(j, "port", p.port);
    read(j, "maxClients", p.maxClients);
}

void to_json(nlohmann::json& j, const TcpConnectParams& p)
{
    j = {{"host", p.host}, {"port", p.port}, {"reconnectSeconds", p.reconnectSeconds}};
}

void from_json(const nlohmann::json& j, TcpConnectParams& p)
{
    read(j, "host", p.host);
    read(j, "port", p.port);
    read(j, "reconnectSeconds", p.reconnectSeconds);
}

void to_json(nlohmann::json& j, const FileParams& p)
{
    j = {{"path", p.path}, {"tail", p.tail}, {"startAt", p.startAt}, {"format", p.format}};
}

void from_json(const nlohmann::json& j, FileParams& p)
{
    read(j, "path", p.path);
    read(j, "tail", p.tail);
    read(j, "startAt", p.startAt);
    read(j, "format", p.format);
}

void to_json(nlohmann::json& j, const ProcessParams& p)
{
    nlohmann::json env = nlohmann::json::object();
    for (auto& [k, v] : p.environment)
        env[k] = v;
    j = {{"command", p.command}, {"arguments", p.arguments}, {"workingDirectory", p.workingDirectory}, {"environment", env}, {"restartOnExit", p.restartOnExit}, {"restartDelaySeconds", p.restartDelaySeconds}};
}

void from_json(const nlohmann::json& j, ProcessParams& p)
{
    read(j, "command", p.command);
    read(j, "arguments", p.arguments);
    read(j, "workingDirectory", p.workingDirectory);
    if (auto it = j.find("environment"); it != j.end() && it->is_object())
    {
        p.environment.clear();
        for (auto& [k, v] : it->items())
            p.environment.emplace_back(k, v.is_string() ? v.get<std::string>() : v.dump());
    }
    read(j, "restartOnExit", p.restartOnExit);
    read(j, "restartDelaySeconds", p.restartDelaySeconds);
}

void to_json(nlohmann::json& j, const DbgviewAgentParams& p)
{
    j = {{"host", p.host}, {"port", p.port}, {"autoLaunchLocal", p.autoLaunchLocal}};
}

void from_json(const nlohmann::json& j, DbgviewAgentParams& p)
{
    read(j, "host", p.host);
    read(j, "port", p.port);
    read(j, "autoLaunchLocal", p.autoLaunchLocal);
}

Encoding SourceConfig::encodingValue() const
{
    return parseEncoding(encoding).value_or(Encoding::Auto);
}

bool SourceConfig::sameRuntimeConfig(const SourceConfig& other) const
{
    return type == other.type && enabled == other.enabled && autoNewline == other.autoNewline && encoding == other.encoding &&
        legacyCharset == other.legacyCharset && processNameOverride == other.processNameOverride && params == other.params;
}

namespace {

const char* const knownKeys[] = {"id", "type", "name", "enabled", "autoNewline", "temporary", "encoding", "legacyCharset", "processNameOverride", "params"};

} // namespace

void to_json(nlohmann::json& j, const SourceConfig& c)
{
    j = c.unknownFields.is_object() ? c.unknownFields : nlohmann::json::object();
    j["id"] = c.id;
    j["type"] = c.type;
    j["name"] = c.name;
    j["enabled"] = c.enabled;
    j["autoNewline"] = c.autoNewline;
    j["temporary"] = c.temporary;
    j["encoding"] = c.encoding;
    j["legacyCharset"] = c.legacyCharset;
    j["processNameOverride"] = c.processNameOverride;
    j["params"] = c.params;
}

void from_json(const nlohmann::json& j, SourceConfig& c)
{
    read(j, "id", c.id);
    read(j, "type", c.type);
    read(j, "name", c.name);
    read(j, "enabled", c.enabled);
    read(j, "autoNewline", c.autoNewline);
    read(j, "temporary", c.temporary);
    read(j, "encoding", c.encoding);
    read(j, "legacyCharset", c.legacyCharset);
    read(j, "processNameOverride", c.processNameOverride);
    if (auto it = j.find("params"); it != j.end() && it->is_object())
        c.params = *it;
    c.unknownFields = nlohmann::json::object();
    for (auto& [key, value] : j.items())
    {
        if (std::find(std::begin(knownKeys), std::end(knownKeys), key) == std::end(knownKeys))
            c.unknownFields[key] = value;
    }
    if (c.id.empty())
        c.id = newSourceId();
}

std::string newSourceId()
{
    static std::atomic<uint32_t> counter{0};
    static const auto seed = std::random_device{}();
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%08x-%012llx-%04x", seed, static_cast<unsigned long long>(now) & 0xffffffffffffull, ++counter & 0xffff);
    return buf;
}

std::string describeSource(const SourceConfig& c)
{
    if (!c.name.empty())
        return c.name;
    if (c.type == "outputdebugstring")
    {
        auto p = c.getParams<OutputDebugStringParams>();
        return p.scope == "global" ? "OutputDebugString (global)" : p.scope == "both" ? "OutputDebugString (local + global)" : "OutputDebugString";
    }
    if (c.type == "udp")
        return "UDP port " + std::to_string(c.getParams<UdpParams>().port);
    if (c.type == "tcp-listen")
        return "TCP port " + std::to_string(c.getParams<TcpListenParams>().port);
    if (c.type == "tcp-connect")
    {
        auto p = c.getParams<TcpConnectParams>();
        return "TCP " + p.host + ":" + std::to_string(p.port);
    }
    if (c.type == "file")
        return platform::pathToUtf8(platform::utf8ToPath(c.getParams<FileParams>().path).filename());
    if (c.type == "process")
        return platform::pathToUtf8(platform::utf8ToPath(c.getParams<ProcessParams>().command).filename());
    if (c.type == "stdin")
        return "stdin";
    if (c.type == "dbgview-agent")
        return "DbgView agent " + c.getParams<DbgviewAgentParams>().host;
    return c.type;
}

// ---------------------------------------------------------------------------------------------------- registry

namespace {

std::string validatePort(int port)
{
    return (port <= 0 || port > 65535) ? "port must be between 1 and 65535" : "";
}

} // namespace

SourceRegistry::SourceRegistry()
{
#ifdef _WIN32
    m_types.push_back(SourceType{"outputdebugstring", "OutputDebugString (Win32)", OutputDebugStringParams{},
        [](const SourceConfig& c) {
            auto scope = c.getParams<OutputDebugStringParams>().scope;
            return (scope == "local" || scope == "global" || scope == "both") ? std::string() : "scope must be local, global or both";
        },
        &createOutputDebugStringSource});
#endif
    m_types.push_back(SourceType{"udp", "UDP listener", UdpParams{},
        [](const SourceConfig& c) { return validatePort(c.getParams<UdpParams>().port); },
        &createUdpSource});
    m_types.push_back(SourceType{"tcp-listen", "TCP listener", TcpListenParams{},
        [](const SourceConfig& c) { return validatePort(c.getParams<TcpListenParams>().port); },
        &createTcpListenSource});
    m_types.push_back(SourceType{"tcp-connect", "TCP client", TcpConnectParams{},
        [](const SourceConfig& c) {
            auto p = c.getParams<TcpConnectParams>();
            return p.host.empty() ? std::string("host is required") : validatePort(p.port);
        },
        &createTcpConnectSource});
    m_types.push_back(SourceType{"file", "File", FileParams{},
        [](const SourceConfig& c) { return c.getParams<FileParams>().path.empty() ? std::string("path is required") : std::string(); },
        &createFileSource});
    m_types.push_back(SourceType{"process", "Process output", ProcessParams{},
        [](const SourceConfig& c) { return c.getParams<ProcessParams>().command.empty() ? std::string("command is required") : std::string(); },
        &createProcessSource});
    m_types.push_back(SourceType{"stdin", "Standard input", nlohmann::json::object(),
        [](const SourceConfig&) { return std::string(); },
        &createStdinSource});
    m_types.push_back(SourceType{"dbgview-agent", "DbgView agent", DbgviewAgentParams{},
        [](const SourceConfig& c) { return validatePort(c.getParams<DbgviewAgentParams>().port); },
        &createDbgviewAgentSource});
}

const SourceRegistry& SourceRegistry::instance()
{
    static const SourceRegistry registry;
    return registry;
}

const SourceType* SourceRegistry::find(const std::string& type) const
{
    for (auto& t : m_types)
        if (t.type == type)
            return &t;
    return nullptr;
}

SourceConfig SourceRegistry::makeDefault(const std::string& type) const
{
    SourceConfig config;
    config.id = newSourceId();
    config.type = type;
    if (auto t = find(type))
        config.params = t->defaultParams;
    return config;
}

SourceConfig SourceRegistry::parseSpec(const std::string& spec) const
{
    auto colon = spec.find(':');
    auto type = trim(spec.substr(0, colon));
    auto t = find(type);
    if (!t)
        throw std::runtime_error("unknown source type '" + type + "'");
    auto config = makeDefault(type);
    if (colon == std::string::npos)
        return config;

    std::string rest = spec.substr(colon + 1);
    size_t pos = 0;
    while (pos <= rest.size())
    {
        auto comma = rest.find(',', pos);
        auto item = trim(rest.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        pos = comma == std::string::npos ? rest.size() + 1 : comma + 1;
        if (item.empty())
            continue;
        auto eq = item.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("expected key=value in '" + item + "'");
        auto key = trim(item.substr(0, eq));
        auto value = trim(item.substr(eq + 1));

        // common fields
        if (key == "name") { config.name = value; continue; }
        if (key == "encoding") { config.encoding = value; continue; }
        if (key == "charset") { config.legacyCharset = value; continue; }
        if (key == "autonewline" || key == "autoNewline") { config.autoNewline = value == "1" || value == "true"; continue; }
        // short aliases
        if (key == "bind") key = "bindAddress";
        if (key == "group") key = "multicastGroup";
        if (key == "args") key = "arguments";
        if (key == "cwd") key = "workingDirectory";

        auto it = config.params.find(key);
        if (it == config.params.end())
            throw std::runtime_error("unknown parameter '" + key + "' for source type '" + type + "'");
        if (it->is_boolean())
            *it = value == "1" || value == "true" || value == "yes" || value == "on";
        else if (it->is_number_integer())
            *it = std::stoll(value);
        else if (it->is_array())
        {
            nlohmann::json list = nlohmann::json::array();
            size_t p = 0;
            while (p <= value.size())
            {
                auto semi = value.find(';', p);
                auto v = value.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
                if (!v.empty())
                    list.push_back(v);
                p = semi == std::string::npos ? value.size() + 1 : semi + 1;
            }
            *it = list;
        }
        else
            *it = value;
    }
    auto error = t->validate(config);
    if (!error.empty())
        throw std::runtime_error(type + ": " + error);
    return config;
}

std::unique_ptr<Source> SourceRegistry::create(const SourceConfig& config, std::string& error) const
{
    auto t = find(config.type);
    if (!t)
    {
        error = "source type '" + config.type + "' is not available on this platform";
        return nullptr;
    }
    error = t->validate(config);
    if (!error.empty())
        return nullptr;
    try
    {
        return t->create(config);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return nullptr;
    }
}

} // namespace dv
