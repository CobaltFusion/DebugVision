// Parsing and validation of proxy settings, and the detection of local SOCKS proxies (suggestions for the proxy dialog)

#include "dvcore/Platform.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <future>
#include <optional>
#include <thread>

namespace dv::platform {

namespace {

std::string toLower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string percentDecode(std::string_view text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        int value = 0;
        if (text[i] == '%' && i + 2 < text.size())
        {
            auto [end, ec] = std::from_chars(text.data() + i + 1, text.data() + i + 3, value, 16);
            if (ec == std::errc() && end == text.data() + i + 3)
            {
                out.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

bool schemeSupported(const std::string& scheme)
{
    static const char* const schemes[] = {"http", "https", "socks4", "socks4a", "socks5", "socks5h"};
    return std::find(std::begin(schemes), std::end(schemes), scheme) != std::end(schemes);
}

} // namespace

bool parseHttpProxy(const std::string& proxy, HttpProxy& out, std::string& error)
{
    out = HttpProxy{};
    std::string_view rest = proxy;
    auto schemeEnd = rest.find("://");
    if (schemeEnd != std::string_view::npos)
    {
        out.scheme = toLower(std::string(rest.substr(0, schemeEnd)));
        rest.remove_prefix(schemeEnd + 3);
    }
    if (!schemeSupported(out.scheme))
    {
        error = "unsupported proxy type '" + out.scheme + "'";
        return false;
    }
    while (!rest.empty() && rest.back() == '/')
        rest.remove_suffix(1);

    auto at = rest.rfind('@');
    if (at != std::string_view::npos)
    {
        auto credentials = rest.substr(0, at);
        rest.remove_prefix(at + 1);
        auto colon = credentials.find(':');
        out.user = percentDecode(credentials.substr(0, colon));
        if (colon != std::string_view::npos)
            out.password = percentDecode(credentials.substr(colon + 1));
    }

    std::string_view port;
    if (!rest.empty() && rest.front() == '[')
    {
        auto close = rest.find(']');
        if (close == std::string_view::npos)
        {
            error = "invalid proxy address '" + proxy + "'";
            return false;
        }
        out.host = std::string(rest.substr(1, close - 1));
        rest.remove_prefix(close + 1);
        if (!rest.empty() && rest.front() == ':')
            port = rest.substr(1);
        else if (!rest.empty())
            out.host.clear();
    }
    else
    {
        auto colon = rest.rfind(':');
        out.host = std::string(rest.substr(0, colon));
        if (colon != std::string_view::npos)
            port = rest.substr(colon + 1);
    }
    if (out.host.empty() || out.host.find_first_of("/?#") != std::string::npos)
    {
        error = "invalid proxy address '" + proxy + "', expected [scheme://][user:password@]host:port";
        return false;
    }
    unsigned value = 0;
    auto [end, ec] = std::from_chars(port.data(), port.data() + port.size(), value);
    if (port.empty() || ec != std::errc() || end != port.data() + port.size() || value == 0 || value > 65535)
    {
        error = "the proxy needs a port, e.g. http://proxy.example.com:8080";
        return false;
    }
    out.port = static_cast<uint16_t>(value);
    return true;
}

std::string validateHttpProxy(const std::string& proxy)
{
    if (proxy.empty() || proxy == "system" || proxy == "direct")
        return {};
    HttpProxy parsed;
    std::string error;
    return parseHttpProxy(proxy, parsed, error) ? std::string() : error;
}

std::vector<uint16_t> defaultSocksPorts()
{
    return {1080, 1081, 9050, 9150};
}

namespace {

std::optional<DetectedProxy> probeSocks(uint16_t port, std::chrono::milliseconds timeout)
{
    std::string error;
    auto stream = tcpConnect("127.0.0.1", port, timeout, error);
    if (!stream)
        return std::nullopt;
    // greeting: SOCKS 5, two methods: 0 (no authentication) and 2 (user name and password)
    if (!stream->write(std::string_view("\x05\x02\x00\x02", 4)))
        return std::nullopt;
    std::string reply;
    auto end = std::chrono::steady_clock::now() + timeout;
    while (reply.size() < 2 && std::chrono::steady_clock::now() < end)
    {
        if (stream->read(reply) == AsyncReader::Status::Eof)
            break;
        if (reply.size() < 2)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    // the server chooses a method: 0 or 2; 0xFF (none acceptable) and anything else is not a usable SOCKS 5 proxy
    if (reply.size() < 2 || reply[0] != '\x05' || (reply[1] != '\x00' && reply[1] != '\x02'))
        return std::nullopt;
    DetectedProxy proxy;
    proxy.proxy = "socks5h://127.0.0.1:" + std::to_string(port);
    if (port == 9050 || port == 9150)
        proxy.note = port == 9050 ? "Tor" : "Tor Browser";
    if (reply[1] == '\x02')
        proxy.note += std::string(proxy.note.empty() ? "" : ", ") + "requires a user name and password";
    return proxy;
}

} // namespace

std::vector<DetectedProxy> detectLocalSocksProxies(const std::vector<uint16_t>& ports, std::chrono::milliseconds timeout)
{
    // in parallel: on Windows, a connection to a closed local port is only refused after about a second of retries
    std::vector<std::future<std::optional<DetectedProxy>>> probes;
    for (auto port : ports)
        probes.push_back(std::async(std::launch::async, probeSocks, port, timeout));
    std::vector<DetectedProxy> found;
    for (auto& probe : probes)
        if (auto proxy = probe.get())
            found.push_back(*proxy);
    return found;
}

} // namespace dv::platform
