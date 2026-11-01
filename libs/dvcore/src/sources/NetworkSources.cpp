// UDP listener, TCP listener, TCP client and the Sysinternals DbgView agent client

#include "Sources.h"

#include "dvcore/Clock.h"
#include "dvcore/FileFormat.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <thread>

namespace dv {

namespace {

using namespace std::chrono_literals;

// --------------------------------------------------------------------------------------------- sender filter

struct SenderRule
{
    std::string address; // exact match, IPv4 or IPv6
    uint32_t network = 0;
    uint32_t mask = 0;
    bool cidr = false;
};

bool parseIpv4(std::string_view text, uint32_t& value)
{
    value = 0;
    for (int i = 0; i < 4; ++i)
    {
        auto dot = text.find('.');
        auto part = text.substr(0, dot);
        unsigned octet = 0;
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), octet);
        if (ec != std::errc() || ptr != part.data() + part.size() || octet > 255)
            return false;
        value = (value << 8) | octet;
        if (i < 3)
        {
            if (dot == std::string_view::npos)
                return false;
            text.remove_prefix(dot + 1);
        }
        else if (dot != std::string_view::npos)
            return false;
    }
    return true;
}

std::vector<SenderRule> parseSenderRules(const std::vector<std::string>& senders)
{
    std::vector<SenderRule> rules;
    for (auto& s : senders)
    {
        SenderRule rule;
        auto slash = s.find('/');
        uint32_t ip = 0;
        unsigned bits = 32;
        if (slash != std::string::npos && parseIpv4(std::string_view(s).substr(0, slash), ip))
        {
            auto bitsText = std::string_view(s).substr(slash + 1);
            std::from_chars(bitsText.data(), bitsText.data() + bitsText.size(), bits);
            bits = std::min(bits, 32u);
            rule.cidr = true;
            rule.mask = bits == 0 ? 0 : ~uint32_t(0) << (32 - bits);
            rule.network = ip & rule.mask;
        }
        else
        {
            rule.address = s;
        }
        rules.push_back(rule);
    }
    return rules;
}

bool senderAllowed(const std::vector<SenderRule>& rules, const std::string& address)
{
    if (rules.empty())
        return true;
    uint32_t ip = 0;
    bool isV4 = parseIpv4(address, ip);
    for (auto& r : rules)
    {
        if (r.cidr ? (isV4 && (ip & r.mask) == r.network) : r.address == address)
            return true;
    }
    return false;
}

// --------------------------------------------------------------------------------------------- UDP

class UdpSource : public Source
{
public:
    explicit UdpSource(const SourceConfig& config) :
        m_config(config),
        m_params(config.getParams<UdpParams>()),
        m_rules(parseSenderRules(m_params.allowedSenders)),
        m_assembler(config.encodingValue(), legacyCodecFor(config), config.autoNewline)
    {
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_sink = &sink;
        std::string error;
        m_socket = platform::UdpSocket::open(m_params.bindAddress, m_params.port, m_params.multicastGroup, error);
        if (!m_socket)
        {
            setStatus(SourceState::Error, "error: " + error);
            sink.internal("UDP source: " + error);
            return;
        }
        m_watch = loop.watch(m_socket->waitHandle(), [this] { onReadable(); });
        auto text = "listening on " + m_params.bindAddress + ":" + std::to_string(m_params.port) +
            (m_params.multicastGroup.empty() ? std::string() : " (group " + m_params.multicastGroup + ")");
        setStatus(SourceState::Running, text);
        sink.internal("UDP source " + text);
    }

    void stop(platform::EventLoop& loop) override
    {
        if (m_watch)
            loop.unwatch(m_watch);
        m_watch = 0;
        if (m_sink)
            m_assembler.flushAll(*m_sink);
        m_socket.reset();
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

private:
    void onReadable()
    {
        std::string data;
        platform::Endpoint from;
        while (m_socket->receive(data, from))
        {
            if (m_paused || !senderAllowed(m_rules, from.address))
                continue;
            data.resize(strnlen(data.data(), data.size())); // text ends at the first NUL, as in DebugView++
            auto steady = nowSteadyNs();
            auto system = nowSystemNs();
            auto origin = originFor(from);
            if (m_params.splitOnNewline)
            {
                m_assembler.add(*m_sink, steady, system, 0, origin, data);
            }
            else
            {
                std::replace(data.begin(), data.end(), '\n', ' ');
                m_assembler.add(*m_sink, steady, system, 0, origin, data);
            }
        }
    }

    uint32_t originFor(const platform::Endpoint& from)
    {
        auto key = from.address + "#" + std::to_string(from.port);
        auto it = m_origins.find(key);
        if (it != m_origins.end())
            return it->second;
        auto name = m_config.processNameOverride.empty() ? expandProcessName(m_params.processName, from.address, from.port) : m_config.processNameOverride;
        auto id = m_sink->origin(name);
        if (m_origins.size() < 4096)
            m_origins.emplace(key, id);
        return id;
    }

    SourceConfig m_config;
    UdpParams m_params;
    std::vector<SenderRule> m_rules;
    MessageAssembler m_assembler;
    std::unique_ptr<platform::UdpSocket> m_socket;
    std::map<std::string, uint32_t> m_origins;
    Sink* m_sink = nullptr;
    platform::EventLoop::Id m_watch = 0;
    bool m_paused = false;
};

// --------------------------------------------------------------------------------------------- TCP connection (shared)

// one TCP stream, split into lines
class TcpConnection
{
public:
    TcpConnection(std::unique_ptr<platform::TcpStream> stream, Encoding encoding, std::shared_ptr<LegacyCodec> legacy, uint32_t origin) :
        m_stream(std::move(stream)),
        m_decoder(encoding, std::move(legacy)),
        m_origin(origin)
    {
    }

    platform::TcpStream& stream() { return *m_stream; }

    // returns false when the connection was closed
    bool read(Sink& sink, bool paused)
    {
        m_buffer.clear();
        auto status = m_stream->read(m_buffer);
        if (!paused)
            m_decoder.feed(m_buffer, [&](std::string_view line) { sink.add(0, m_origin, line); });
        if (status == platform::AsyncReader::Status::Eof)
        {
            m_decoder.flush([&](std::string_view line) { sink.add(0, m_origin, line); });
            return false;
        }
        return true;
    }

private:
    std::unique_ptr<platform::TcpStream> m_stream;
    LineDecoder m_decoder;
    uint32_t m_origin;
    std::string m_buffer;
};

class TcpListenSource : public Source
{
public:
    explicit TcpListenSource(const SourceConfig& config) :
        m_config(config),
        m_params(config.getParams<TcpListenParams>()),
        m_legacy(legacyCodecFor(config))
    {
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        std::string error;
        m_listener = platform::TcpListener::open(m_params.bindAddress, m_params.port, error);
        if (!m_listener)
        {
            setStatus(SourceState::Error, "error: " + error);
            sink.internal("TCP source: " + error);
            return;
        }
        m_watch = loop.watch(m_listener->waitHandle(), [this] { onAccept(); });
        setStatus(SourceState::Running, "listening on " + m_params.bindAddress + ":" + std::to_string(m_params.port));
    }

    void stop(platform::EventLoop& loop) override
    {
        for (auto& [id, conn] : m_connections)
        {
            loop.unwatch(id);
            m_retired.push_back(std::move(conn));
        }
        m_connections.clear();
        if (m_watch)
            loop.unwatch(m_watch);
        m_watch = 0;
        m_listener.reset();
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

private:
    void onAccept()
    {
        while (auto stream = m_listener->accept())
        {
            if (static_cast<int>(m_connections.size()) >= m_params.maxClients)
            {
                m_sink->internal("TCP source: rejected connection from " + stream->peer().address + ", maximum number of clients reached");
                continue;
            }
            auto peer = stream->peer();
            auto name = m_config.processNameOverride.empty() ? expandProcessName("[TCP {ip}:{port}]", peer.address, peer.port) : m_config.processNameOverride;
            auto conn = std::make_unique<TcpConnection>(std::move(stream), m_config.encodingValue(), m_legacy, m_sink->origin(name));
            auto handle = conn->stream().waitHandle();
            auto id = std::make_shared<platform::EventLoop::Id>(0);
            *id = m_loop->watch(handle, [this, id] { onData(*id); });
            m_connections.emplace(*id, std::move(conn));
            m_sink->internal("TCP connection from " + peer.address + ":" + std::to_string(peer.port));
        }
    }

    void onData(platform::EventLoop::Id id)
    {
        auto it = m_connections.find(id);
        if (it == m_connections.end())
            return;
        if (!it->second->read(*m_sink, m_paused))
        {
            auto peer = it->second->stream().peer();
            m_sink->internal("TCP connection from " + peer.address + ":" + std::to_string(peer.port) + " closed");
            m_loop->unwatch(id);
            m_retired.push_back(std::move(it->second));
            m_connections.erase(it);
            m_loop->post([this] { m_retired.clear(); });
        }
    }

    SourceConfig m_config;
    TcpListenParams m_params;
    std::shared_ptr<LegacyCodec> m_legacy;
    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    std::unique_ptr<platform::TcpListener> m_listener;
    platform::EventLoop::Id m_watch = 0;
    std::map<platform::EventLoop::Id, std::unique_ptr<TcpConnection>> m_connections;
    std::vector<std::unique_ptr<TcpConnection>> m_retired;
    bool m_paused = false;
};

// Connects on a helper thread (connect and DNS lookups block), then hands the stream to the event loop.
// Reconnects with backoff when the connection fails or closes.
class ConnectingSource : public Source
{
public:
    ConnectingSource(std::string host, uint16_t port, int reconnectSeconds) :
        m_host(std::move(host)),
        m_port(port),
        m_reconnectDelay(std::chrono::seconds(std::max(1, reconnectSeconds))),
        m_baseDelay(m_reconnectDelay),
        m_alive(std::make_shared<bool>(true))
    {
    }

    ~ConnectingSource() override
    {
        if (m_thread.joinable())
            m_thread.join();
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        connect();
    }

    void stop(platform::EventLoop& loop) override
    {
        m_stopped = true;
        if (m_watch)
            loop.unwatch(m_watch);
        if (m_timer)
            loop.cancelTimer(m_timer);
        m_watch = 0;
        m_timer = 0;
        if (m_stream)
            onDisconnected();
        m_stream.reset();
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

protected:
    virtual void onConnected(platform::TcpStream& stream) = 0;
    // returns false when the connection should be closed
    virtual bool onData(std::string_view data) = 0;
    virtual void onDisconnected() {}

    std::string endpointText() const { return m_host + ":" + std::to_string(m_port); }

    Sink* m_sink = nullptr;
    bool m_paused = false;

private:
    void connect()
    {
        if (m_thread.joinable())
            m_thread.join();
        setStatus(SourceState::Starting, "connecting to " + endpointText());
        std::weak_ptr<bool> alive = m_alive;
        auto loop = m_loop;
        auto host = m_host;
        auto port = m_port;
        m_thread = std::thread([this, alive, loop, host, port] {
            std::string error;
            auto stream = platform::tcpConnect(host, port, 5000ms, error);
            std::shared_ptr<platform::TcpStream> shared(std::move(stream));
            loop->post([this, alive, shared, error] {
                if (alive.expired() || m_stopped)
                    return;
                onConnectResult(shared, error);
            });
        });
    }

    void onConnectResult(std::shared_ptr<platform::TcpStream> stream, const std::string& error)
    {
        if (!stream)
        {
            setStatus(SourceState::Waiting, "error: " + error + ", retrying in " + std::to_string(m_reconnectDelay.count()) + "s");
            scheduleReconnect();
            return;
        }
        m_stream = std::move(stream);
        m_reconnectDelay = m_baseDelay;
        setStatus(SourceState::Running, "connected to " + endpointText());
        m_sink->internal("Connected to " + endpointText());
        onConnected(*m_stream);
        m_watch = m_loop->watch(m_stream->waitHandle(), [this] { onReadable(); });
    }

    void onReadable()
    {
        m_buffer.clear();
        auto status = m_stream->read(m_buffer);
        bool keep = m_buffer.empty() || onData(m_buffer);
        if (status == platform::AsyncReader::Status::Eof || !keep)
        {
            m_sink->internal("Connection to " + endpointText() + " closed");
            onDisconnected();
            m_loop->unwatch(m_watch);
            m_watch = 0;
            auto old = std::move(m_stream);
            m_loop->post([old] {}); // destroy after the current callback
            setStatus(SourceState::Waiting, "disconnected, retrying in " + std::to_string(m_reconnectDelay.count()) + "s");
            scheduleReconnect();
        }
    }

    void scheduleReconnect()
    {
        if (m_stopped)
            return;
        m_timer = m_loop->addTimer(std::chrono::steady_clock::now() + m_reconnectDelay, [this] {
            m_timer = 0;
            connect();
        });
        m_reconnectDelay = std::min<std::chrono::seconds>(m_reconnectDelay * 2, 60s);
    }

    std::string m_host;
    uint16_t m_port;
    std::chrono::seconds m_reconnectDelay;
    std::chrono::seconds m_baseDelay;
    std::shared_ptr<bool> m_alive;
    std::thread m_thread;
    platform::EventLoop* m_loop = nullptr;
    std::shared_ptr<platform::TcpStream> m_stream;
    platform::EventLoop::Id m_watch = 0;
    platform::EventLoop::Id m_timer = 0;
    std::string m_buffer;
    bool m_stopped = false;
};

class TcpConnectSource : public ConnectingSource
{
public:
    explicit TcpConnectSource(const SourceConfig& config) :
        ConnectingSource(config.getParams<TcpConnectParams>().host, config.getParams<TcpConnectParams>().port, config.getParams<TcpConnectParams>().reconnectSeconds),
        m_config(config),
        m_decoder(config.encodingValue(), legacyCodecFor(config))
    {
    }

protected:
    void onConnected(platform::TcpStream&) override
    {
        m_decoder.reset();
        auto name = m_config.processNameOverride.empty() ? "[TCP " + endpointText() + "]" : m_config.processNameOverride;
        m_origin = m_sink->origin(name);
    }

    bool onData(std::string_view data) override
    {
        if (!m_paused)
            m_decoder.feed(data, [this](std::string_view line) { m_sink->add(0, m_origin, line); });
        return true;
    }

    void onDisconnected() override
    {
        m_decoder.flush([this](std::string_view line) { m_sink->add(0, m_origin, line); });
    }

private:
    SourceConfig m_config;
    LineDecoder m_decoder;
    uint32_t m_origin = 0;
};

// --------------------------------------------------------------------------------------------- DbgView agent

// Protocol of the Sysinternals DebugView agent ('dbgview.exe /a', port 2020), as implemented in DebugView++'s DbgviewReader
namespace agent {
constexpr uint32_t base = 0x83050000;
constexpr uint32_t captureKernelEnable = base + 0x00;
constexpr uint32_t verboseKernelMessagesEnable = base + 0x08;
constexpr uint32_t passThroughEnable = base + 0x14;
constexpr uint32_t captureWin32Enable = base + 0x18;
constexpr uint32_t requestQueryPerformanceFrequency = base + 0x28;
constexpr uint32_t forceCarriageReturnsEnable = base + 0x34;
constexpr uint32_t forceCarriageReturnsDisable = base + 0x38;
constexpr char columnOneMark = 1;
constexpr char columnTwoMark = 2;
} // namespace agent

class DbgviewAgentSource : public ConnectingSource
{
public:
    explicit DbgviewAgentSource(const SourceConfig& config) :
        ConnectingSource(config.getParams<DbgviewAgentParams>().host, config.getParams<DbgviewAgentParams>().port, 5),
        m_config(config),
        m_assembler(Encoding::Utf8OrLegacy, legacyCodecFor(config), true)
    {
    }

protected:
    void onConnected(platform::TcpStream& stream) override
    {
        m_received.clear();
        m_haveFrequency = false;
        m_first = true;
        m_origin = m_sink->origin(m_config.processNameOverride.empty() ? "[tcp]" : m_config.processNameOverride);
        std::string request;
        for (auto cmd : {agent::requestQueryPerformanceFrequency, agent::captureKernelEnable, agent::verboseKernelMessagesEnable, agent::captureWin32Enable, agent::passThroughEnable,
                 m_config.autoNewline ? agent::forceCarriageReturnsEnable : agent::forceCarriageReturnsDisable})
            appendDword(request, cmd);
        stream.write(request);
    }

    bool onData(std::string_view data) override
    {
        m_received.append(data);
        size_t pos = 0;
        if (!m_haveFrequency)
        {
            if (m_received.size() < 4)
                return true;
            m_frequency = readDword(m_received, 0);
            m_haveFrequency = true;
            pos = 4;
            if (m_frequency == 0)
            {
                m_sink->internal("DbgView agent: invalid handshake");
                return false;
            }
        }
        while (m_received.size() - pos >= 4)
        {
            auto length = readDword(m_received, pos);
            if (length >= 0x7fffffff)
            {
                m_sink->internal("DbgView agent: error parsing message length");
                return false;
            }
            if (length == 0) // keep alive
            {
                pos += 4;
                continue;
            }
            if (m_received.size() - pos - 4 < length)
                break;
            parseFrame(std::string_view(m_received).substr(pos + 4, length));
            pos += 4 + length;
        }
        m_received.erase(0, pos);
        return true;
    }

private:
    static void appendDword(std::string& out, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }

    static uint32_t readDword(std::string_view data, size_t pos)
    {
        uint32_t v = 0;
        std::memcpy(&v, data.data() + pos, 4); // little endian on all supported platforms
        return v;
    }

    void parseFrame(std::string_view frame)
    {
        size_t pos = 0;
        while (frame.size() - pos >= 20)
        {
            uint64_t fileTime = 0;
            int64_t qpc = 0;
            std::memcpy(&fileTime, frame.data() + pos + 4, 8);
            std::memcpy(&qpc, frame.data() + pos + 12, 8);
            pos += 20;
            if (m_first)
            {
                m_qpc0 = qpc;
                m_steady0 = nowSteadyNs();
                m_first = false;
            }
            uint32_t pid = 0;
            // the pid marker is checked for every record (DebugView++ only checked the first record of a frame)
            if (pos < frame.size() && frame[pos] == agent::columnOneMark)
            {
                auto end = frame.find(agent::columnTwoMark, pos + 1);
                if (end == std::string_view::npos)
                    return;
                auto digits = frame.substr(pos + 1, end - pos - 1);
                std::from_chars(digits.data(), digits.data() + digits.size(), pid);
                pos = end + 1;
                if (pos < frame.size() && frame[pos] == ' ')
                    ++pos;
            }
            auto nul = frame.find('\0', pos);
            auto text = frame.substr(pos, nul == std::string_view::npos ? std::string_view::npos : nul - pos);
            pos = nul == std::string_view::npos ? frame.size() : nul + 1;
            pos = (pos + 3) & ~size_t(3); // records are padded to multiples of 4 bytes

            if (m_paused)
                continue;
            auto steady = m_steady0 + static_cast<int64_t>(static_cast<double>(qpc - m_qpc0) * 1e9 / m_frequency);
            m_assembler.add(*m_sink, steady, fileTimeToUnixNs(fileTime), pid, m_origin, text);
        }
    }

    SourceConfig m_config;
    MessageAssembler m_assembler;
    std::string m_received;
    bool m_haveFrequency = false;
    uint32_t m_frequency = 0;
    bool m_first = true;
    int64_t m_qpc0 = 0;
    int64_t m_steady0 = 0;
    uint32_t m_origin = 0;
};

} // namespace

std::unique_ptr<Source> createUdpSource(const SourceConfig& config)
{
    return std::make_unique<UdpSource>(config);
}

std::unique_ptr<Source> createTcpListenSource(const SourceConfig& config)
{
    return std::make_unique<TcpListenSource>(config);
}

std::unique_ptr<Source> createTcpConnectSource(const SourceConfig& config)
{
    return std::make_unique<TcpConnectSource>(config);
}

std::unique_ptr<Source> createDbgviewAgentSource(const SourceConfig& config)
{
    return std::make_unique<DbgviewAgentSource>(config);
}

} // namespace dv
