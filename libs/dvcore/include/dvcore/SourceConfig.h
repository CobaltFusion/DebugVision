#pragma once

#include "dvcore/Encoding.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dv {

class Source;

// Type specific parameters. They are stored as JSON inside SourceConfig, so fields this build does not know are preserved.

struct OutputDebugStringParams // Windows only
{
    std::string scope = "local"; // local | global | both
};

struct UdpParams
{
    std::string bindAddress = "0.0.0.0";
    uint16_t port = 2020;
    std::string multicastGroup;              // optional
    std::vector<std::string> allowedSenders; // optional: addresses or CIDR ranges
    std::string processName = "[UDP {ip}]";
    bool splitOnNewline = true;
};

struct TcpListenParams
{
    std::string bindAddress = "0.0.0.0";
    uint16_t port = 2021;
    int maxClients = 16;
};

struct TcpConnectParams
{
    std::string host = "127.0.0.1";
    uint16_t port = 2021;
    int reconnectSeconds = 5;
};

struct FileParams
{
    std::string path;
    bool tail = true;
    std::string startAt = "beginning"; // beginning | end
    std::string format = "auto";       // auto | text | dblog | sysinternals
};

struct ProcessParams
{
    std::string command;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::vector<std::pair<std::string, std::string>> environment;
    bool restartOnExit = false;
    int restartDelaySeconds = 5;
};

struct DbgviewAgentParams
{
    std::string host = "127.0.0.1";
    uint16_t port = 2020;
    bool autoLaunchLocal = false; // Windows only: start 'dbgview.exe /a' when it is next to the executable
};

struct HttpParams
{
    std::string url;                   // http:// or https://
    bool tail = true;
    int pollMilliseconds = 1000;
    std::string startAt = "beginning"; // beginning | end
    std::string format = "auto";       // auto | text | dblog | sysinternals
    std::string proxy;                 // "" = the default proxy (see applyHttpProxyDefault), otherwise see platform::HttpProxy
};

void to_json(nlohmann::json& j, const OutputDebugStringParams& p);
void from_json(const nlohmann::json& j, OutputDebugStringParams& p);
void to_json(nlohmann::json& j, const UdpParams& p);
void from_json(const nlohmann::json& j, UdpParams& p);
void to_json(nlohmann::json& j, const TcpListenParams& p);
void from_json(const nlohmann::json& j, TcpListenParams& p);
void to_json(nlohmann::json& j, const TcpConnectParams& p);
void from_json(const nlohmann::json& j, TcpConnectParams& p);
void to_json(nlohmann::json& j, const FileParams& p);
void from_json(const nlohmann::json& j, FileParams& p);
void to_json(nlohmann::json& j, const ProcessParams& p);
void from_json(const nlohmann::json& j, ProcessParams& p);
void to_json(nlohmann::json& j, const DbgviewAgentParams& p);
void from_json(const nlohmann::json& j, DbgviewAgentParams& p);
void to_json(nlohmann::json& j, const HttpParams& p);
void from_json(const nlohmann::json& j, HttpParams& p);

struct SourceConfig
{
    std::string id; // unique, stable across restarts
    std::string type;
    std::string name;
    bool enabled = true;
    bool autoNewline = true;
    bool temporary = false;            // not restored at the next start (e.g. dropped files)
    std::string encoding = "auto";     // see dv::Encoding
    std::string legacyCharset;         // "" = platform default
    std::string processNameOverride;   // optional
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json unknownFields = nlohmann::json::object(); // preserved for forward compatibility

    // merges 'value' into params (keeps fields the struct does not know)
    template <typename T>
    void setParams(const T& value)
    {
        nlohmann::json j = value;
        for (auto& [key, v] : j.items())
            params[key] = v;
    }

    template <typename T>
    T getParams() const
    {
        T value{};
        if (params.is_object())
            from_json(params, value);
        return value;
    }

    Encoding encodingValue() const;
    // compares everything that requires a restart of the source when changed
    bool sameRuntimeConfig(const SourceConfig& other) const;
};

void to_json(nlohmann::json& j, const SourceConfig& c);
void from_json(const nlohmann::json& j, SourceConfig& c);

std::string newSourceId();

// fills in 'proxy' for the http sources that do not have their own proxy setting, e.g. the proxy of the application
// settings or of 'dvcli --proxy'; done on the configs given to Ingest, so the saved source settings keep "" (= default)
void applyHttpProxyDefault(std::vector<SourceConfig>& configs, const std::string& proxy);

struct SourceType
{
    std::string type;
    std::string displayName;
    nlohmann::json defaultParams;
    // returns an empty string when valid
    std::function<std::string(const SourceConfig&)> validate;
    std::function<std::unique_ptr<Source>(const SourceConfig&)> create;
};

// The source types available on this platform. 'outputdebugstring' is only registered on Windows.
class SourceRegistry
{
public:
    static const SourceRegistry& instance();

    const SourceType* find(const std::string& type) const;
    const std::vector<SourceType>& types() const { return m_types; }
    bool isAvailable(const std::string& type) const { return find(type) != nullptr; }

    SourceConfig makeDefault(const std::string& type) const;
    // parses "type:key=value,key=value"; list values are separated by ';'. Throws std::runtime_error.
    SourceConfig parseSpec(const std::string& spec) const;
    std::unique_ptr<Source> create(const SourceConfig& config, std::string& error) const;

private:
    SourceRegistry();
    std::vector<SourceType> m_types;
};

// default display name for a config, e.g. "UDP port 2020", "app.log"
std::string describeSource(const SourceConfig& config);

} // namespace dv
