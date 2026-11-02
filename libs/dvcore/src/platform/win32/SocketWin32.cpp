#include "dvcore/Platform.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace dv::platform {

std::string win32ErrorMessage(DWORD error);

namespace {

void ensureWinsock()
{
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

std::string socketError()
{
    return win32ErrorMessage(static_cast<DWORD>(WSAGetLastError()));
}

Endpoint toEndpoint(const sockaddr_storage& addr)
{
    Endpoint ep;
    char host[INET6_ADDRSTRLEN] = {};
    if (addr.ss_family == AF_INET)
    {
        auto& in = reinterpret_cast<const sockaddr_in&>(addr);
        inet_ntop(AF_INET, &in.sin_addr, host, sizeof(host));
        ep.port = ntohs(in.sin_port);
    }
    else if (addr.ss_family == AF_INET6)
    {
        auto& in6 = reinterpret_cast<const sockaddr_in6&>(addr);
        inet_ntop(AF_INET6, &in6.sin6_addr, host, sizeof(host));
        ep.port = ntohs(in6.sin6_port);
    }
    ep.address = host;
    return ep;
}

addrinfo* resolve(const std::string& host, uint16_t port, int socktype, bool passive, std::string& error)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    addrinfo* result = nullptr;
    auto portText = std::to_string(port);
    auto rc = getaddrinfo(host.empty() ? nullptr : host.c_str(), portText.c_str(), &hints, &result);
    if (rc != 0)
    {
        error = "cannot resolve '" + host + "': " + win32ErrorMessage(static_cast<DWORD>(rc));
        return nullptr;
    }
    return result;
}

// A socket whose network events are signaled through a WSAEVENT
class EventSocket
{
public:
    EventSocket(SOCKET s, long events) :
        m_socket(s),
        m_event(WSACreateEvent())
    {
        WSAEventSelect(m_socket, m_event, events);
    }

    ~EventSocket()
    {
        closesocket(m_socket);
        WSACloseEvent(m_event);
    }

    SOCKET socket() const { return m_socket; }
    HANDLE event() const { return m_event; }

    // resets the event; must be followed by one more attempt to read, see receive()
    void resetEvent()
    {
        WSANETWORKEVENTS ne;
        WSAEnumNetworkEvents(m_socket, m_event, &ne);
        m_closed = m_closed || (ne.lNetworkEvents & FD_CLOSE) != 0;
    }

    bool closed() const { return m_closed; }

private:
    SOCKET m_socket;
    WSAEVENT m_event;
    bool m_closed = false;
};

class UdpSocketWin32 : public UdpSocket
{
public:
    explicit UdpSocketWin32(SOCKET s) :
        m_socket(s, FD_READ)
    {
    }

    NativeHandle waitHandle() const override { return m_socket.event(); }

    bool receive(std::string& data, Endpoint& from) override
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            sockaddr_storage addr{};
            int addrLen = sizeof(addr);
            auto n = recvfrom(m_socket.socket(), m_buffer, sizeof(m_buffer), 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
            if (n >= 0)
            {
                data.assign(m_buffer, static_cast<size_t>(n));
                from = toEndpoint(addr);
                return true;
            }
            auto error = WSAGetLastError();
            if (error == WSAEMSGSIZE)
            {
                // datagram larger than the buffer, the first part was received
                data.assign(m_buffer, sizeof(m_buffer));
                from = toEndpoint(addr);
                return true;
            }
            if (error != WSAEWOULDBLOCK && error != WSAECONNRESET)
                return false;
            if (attempt == 0)
                m_socket.resetEvent(); // a datagram that arrives after this reset is caught by the second attempt
        }
        return false;
    }

private:
    EventSocket m_socket;
    char m_buffer[65536];
};

class TcpStreamWin32 : public TcpStream
{
public:
    TcpStreamWin32(SOCKET s, Endpoint peer) :
        m_socket(s, FD_READ | FD_CLOSE),
        m_peer(std::move(peer))
    {
    }

    NativeHandle waitHandle() const override { return m_socket.event(); }
    Endpoint peer() const override { return m_peer; }

    AsyncReader::Status read(std::string& out) override
    {
        m_socket.resetEvent();
        for (;;)
        {
            auto n = recv(m_socket.socket(), m_buffer, sizeof(m_buffer), 0);
            if (n > 0)
            {
                out.append(m_buffer, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
                return AsyncReader::Status::Eof;
            auto error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
                return AsyncReader::Status::Ok;
            return AsyncReader::Status::Eof;
        }
    }

    bool write(std::string_view data) override
    {
        while (!data.empty())
        {
            auto n = send(m_socket.socket(), data.data(), static_cast<int>(data.size()), 0);
            if (n == SOCKET_ERROR)
            {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                    return false;
                fd_set set;
                FD_ZERO(&set);
                FD_SET(m_socket.socket(), &set);
                timeval tv{5, 0};
                if (select(0, nullptr, &set, nullptr, &tv) <= 0)
                    return false;
                continue;
            }
            data.remove_prefix(static_cast<size_t>(n));
        }
        return true;
    }

private:
    EventSocket m_socket;
    Endpoint m_peer;
    char m_buffer[65536];
};

class TcpListenerWin32 : public TcpListener
{
public:
    explicit TcpListenerWin32(SOCKET s) :
        m_socket(s, FD_ACCEPT)
    {
    }

    NativeHandle waitHandle() const override { return m_socket.event(); }

    std::unique_ptr<TcpStream> accept() override
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            sockaddr_storage addr{};
            int addrLen = sizeof(addr);
            auto s = ::accept(m_socket.socket(), reinterpret_cast<sockaddr*>(&addr), &addrLen);
            if (s != INVALID_SOCKET)
            {
                WSAEventSelect(s, nullptr, 0); // accepted sockets inherit the listener's event selection
                return std::make_unique<TcpStreamWin32>(s, toEndpoint(addr));
            }
            if (attempt == 0)
                m_socket.resetEvent();
        }
        return nullptr;
    }

private:
    EventSocket m_socket;
};

bool joinMulticast(SOCKET s, int family, const std::string& group, std::string& error)
{
    if (family == AF_INET)
    {
        ip_mreq mreq{};
        if (inet_pton(AF_INET, group.c_str(), &mreq.imr_multiaddr) != 1)
        {
            error = "invalid multicast group '" + group + "'";
            return false;
        }
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0)
        {
            error = "joining multicast group failed: " + socketError();
            return false;
        }
        return true;
    }
    ipv6_mreq mreq6{};
    if (inet_pton(AF_INET6, group.c_str(), &mreq6.ipv6mr_multiaddr) != 1)
    {
        error = "invalid multicast group '" + group + "'";
        return false;
    }
    if (setsockopt(s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq6), sizeof(mreq6)) != 0)
    {
        error = "joining multicast group failed: " + socketError();
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<UdpSocket> UdpSocket::open(const std::string& bindAddress, uint16_t port, const std::string& multicastGroup, std::string& error)
{
    ensureWinsock();
    auto ai = resolve(bindAddress.empty() ? "0.0.0.0" : bindAddress, port, SOCK_DGRAM, true, error);
    if (!ai)
        return nullptr;
    auto s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        error = socketError();
        freeaddrinfo(ai);
        return nullptr;
    }
    if (!multicastGroup.empty())
    {
        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    }
    if (bind(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0)
    {
        error = "cannot bind to " + bindAddress + ":" + std::to_string(port) + ": " + socketError();
        closesocket(s);
        freeaddrinfo(ai);
        return nullptr;
    }
    auto family = ai->ai_family;
    freeaddrinfo(ai);
    if (!multicastGroup.empty() && !joinMulticast(s, family, multicastGroup, error))
    {
        closesocket(s);
        return nullptr;
    }
    return std::make_unique<UdpSocketWin32>(s);
}

std::unique_ptr<TcpListener> TcpListener::open(const std::string& bindAddress, uint16_t port, std::string& error)
{
    ensureWinsock();
    auto ai = resolve(bindAddress.empty() ? "0.0.0.0" : bindAddress, port, SOCK_STREAM, true, error);
    if (!ai)
        return nullptr;
    auto s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET || bind(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0 || listen(s, SOMAXCONN) != 0)
    {
        error = "cannot listen on " + bindAddress + ":" + std::to_string(port) + ": " + socketError();
        if (s != INVALID_SOCKET)
            closesocket(s);
        freeaddrinfo(ai);
        return nullptr;
    }
    freeaddrinfo(ai);
    return std::make_unique<TcpListenerWin32>(s);
}

std::unique_ptr<TcpStream> tcpConnect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout, std::string& error)
{
    ensureWinsock();
    auto ai = resolve(host, port, SOCK_STREAM, false, error);
    if (!ai)
        return nullptr;
    for (auto p = ai; p; p = p->ai_next)
    {
        auto s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET)
            continue;
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
        connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen));
        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(s, &writeSet);
        FD_SET(s, &errorSet);
        timeval tv{static_cast<long>(timeout.count() / 1000), static_cast<long>((timeout.count() % 1000) * 1000)};
        if (select(0, nullptr, &writeSet, &errorSet, &tv) > 0 && FD_ISSET(s, &writeSet))
        {
            sockaddr_storage addr{};
            std::memcpy(&addr, p->ai_addr, std::min(sizeof(addr), static_cast<size_t>(p->ai_addrlen)));
            freeaddrinfo(ai);
            return std::make_unique<TcpStreamWin32>(s, toEndpoint(addr));
        }
        error = "cannot connect to " + host + ":" + std::to_string(port);
        closesocket(s);
    }
    freeaddrinfo(ai);
    if (error.empty())
        error = "cannot connect to " + host + ":" + std::to_string(port);
    return nullptr;
}

std::unique_ptr<UdpSender> UdpSender::open(const std::string& address, uint16_t port, std::string& error)
{
    class UdpSenderWin32 : public UdpSender
    {
    public:
        UdpSenderWin32(SOCKET s, const addrinfo& ai) :
            m_socket(s),
            m_addrLen(static_cast<int>(ai.ai_addrlen))
        {
            std::memcpy(&m_addr, ai.ai_addr, ai.ai_addrlen);
        }

        ~UdpSenderWin32() override { closesocket(m_socket); }

        bool send(std::string_view data, std::string& error) override
        {
            if (sendto(m_socket, data.data(), static_cast<int>(data.size()), 0, reinterpret_cast<const sockaddr*>(&m_addr), m_addrLen) != SOCKET_ERROR)
                return true;
            error = socketError();
            return false;
        }

    private:
        SOCKET m_socket;
        sockaddr_storage m_addr{};
        int m_addrLen;
    };

    ensureWinsock();
    auto ai = resolve(address, port, SOCK_DGRAM, false, error);
    if (!ai)
        return nullptr;
    auto s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        error = socketError();
        freeaddrinfo(ai);
        return nullptr;
    }
    BOOL broadcast = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));
    auto sender = std::make_unique<UdpSenderWin32>(s, *ai);
    freeaddrinfo(ai);
    return sender;
}

bool sendUdp(const std::string& address, uint16_t port, std::string_view data, std::string& error)
{
    auto sender = UdpSender::open(address, port, error);
    return sender && sender->send(data, error);
}

} // namespace dv::platform
