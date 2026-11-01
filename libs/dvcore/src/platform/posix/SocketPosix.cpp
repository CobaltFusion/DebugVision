#include "dvcore/Platform.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dv::platform {

namespace {

void setNonBlocking(int fd)
{
    auto flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
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
        error = "cannot resolve '" + host + "': " + gai_strerror(rc);
        return nullptr;
    }
    return result;
}

class UdpSocketPosix : public UdpSocket
{
public:
    explicit UdpSocketPosix(int fd) :
        m_fd(fd)
    {
        setNonBlocking(m_fd);
    }

    ~UdpSocketPosix() override { ::close(m_fd); }

    NativeHandle waitHandle() const override { return m_fd; }

    bool receive(std::string& data, Endpoint& from) override
    {
        sockaddr_storage addr{};
        socklen_t addrLen = sizeof(addr);
        auto n = recvfrom(m_fd, m_buffer, sizeof(m_buffer), 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        if (n < 0)
            return false;
        data.assign(m_buffer, static_cast<size_t>(n));
        from = toEndpoint(addr);
        return true;
    }

private:
    int m_fd;
    char m_buffer[65536];
};

class TcpStreamPosix : public TcpStream
{
public:
    TcpStreamPosix(int fd, Endpoint peer) :
        m_fd(fd),
        m_peer(std::move(peer))
    {
        setNonBlocking(m_fd);
    }

    ~TcpStreamPosix() override { ::close(m_fd); }

    NativeHandle waitHandle() const override { return m_fd; }
    Endpoint peer() const override { return m_peer; }

    AsyncReader::Status read(std::string& out) override
    {
        char buffer[65536];
        for (;;)
        {
            auto n = ::recv(m_fd, buffer, sizeof(buffer), 0);
            if (n > 0)
            {
                out.append(buffer, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
                return AsyncReader::Status::Eof;
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return AsyncReader::Status::Ok;
            return AsyncReader::Status::Eof;
        }
    }

    bool write(std::string_view data) override
    {
        while (!data.empty())
        {
#ifdef MSG_NOSIGNAL
            auto n = ::send(m_fd, data.data(), data.size(), MSG_NOSIGNAL);
#else
            auto n = ::send(m_fd, data.data(), data.size(), 0);
#endif
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    return false;
                pollfd pfd{m_fd, POLLOUT, 0};
                if (::poll(&pfd, 1, 5000) <= 0)
                    return false;
                continue;
            }
            data.remove_prefix(static_cast<size_t>(n));
        }
        return true;
    }

private:
    int m_fd;
    Endpoint m_peer;
};

class TcpListenerPosix : public TcpListener
{
public:
    explicit TcpListenerPosix(int fd) :
        m_fd(fd)
    {
        setNonBlocking(m_fd);
    }

    ~TcpListenerPosix() override { ::close(m_fd); }

    NativeHandle waitHandle() const override { return m_fd; }

    std::unique_ptr<TcpStream> accept() override
    {
        sockaddr_storage addr{};
        socklen_t addrLen = sizeof(addr);
        auto fd = ::accept(m_fd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        if (fd < 0)
            return nullptr;
        return std::make_unique<TcpStreamPosix>(fd, toEndpoint(addr));
    }

private:
    int m_fd;
};

bool joinMulticast(int fd, int family, const std::string& group, std::string& error)
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
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
        {
            error = std::string("joining multicast group failed: ") + std::strerror(errno);
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
    if (setsockopt(fd, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq6, sizeof(mreq6)) != 0)
    {
        error = std::string("joining multicast group failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

} // namespace

std::unique_ptr<UdpSocket> UdpSocket::open(const std::string& bindAddress, uint16_t port, const std::string& multicastGroup, std::string& error)
{
    auto ai = resolve(bindAddress.empty() ? "0.0.0.0" : bindAddress, port, SOCK_DGRAM, true, error);
    if (!ai)
        return nullptr;
    auto fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
    {
        error = std::strerror(errno);
        freeaddrinfo(ai);
        return nullptr;
    }
    if (!multicastGroup.empty())
    {
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
    }
    if (::bind(fd, ai->ai_addr, ai->ai_addrlen) != 0)
    {
        error = "cannot bind to " + bindAddress + ":" + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        freeaddrinfo(ai);
        return nullptr;
    }
    auto family = ai->ai_family;
    freeaddrinfo(ai);
    if (!multicastGroup.empty() && !joinMulticast(fd, family, multicastGroup, error))
    {
        ::close(fd);
        return nullptr;
    }
    return std::make_unique<UdpSocketPosix>(fd);
}

std::unique_ptr<TcpListener> TcpListener::open(const std::string& bindAddress, uint16_t port, std::string& error)
{
    auto ai = resolve(bindAddress.empty() ? "0.0.0.0" : bindAddress, port, SOCK_STREAM, true, error);
    if (!ai)
        return nullptr;
    auto fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    int reuse = 1;
    if (fd >= 0)
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (fd < 0 || ::bind(fd, ai->ai_addr, ai->ai_addrlen) != 0 || ::listen(fd, SOMAXCONN) != 0)
    {
        error = "cannot listen on " + bindAddress + ":" + std::to_string(port) + ": " + std::strerror(errno);
        if (fd >= 0)
            ::close(fd);
        freeaddrinfo(ai);
        return nullptr;
    }
    freeaddrinfo(ai);
    return std::make_unique<TcpListenerPosix>(fd);
}

std::unique_ptr<TcpStream> tcpConnect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout, std::string& error)
{
    auto ai = resolve(host, port, SOCK_STREAM, false, error);
    if (!ai)
        return nullptr;
    for (auto p = ai; p; p = p->ai_next)
    {
        auto fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
            continue;
        setNonBlocking(fd);
        auto rc = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS)
        {
            pollfd pfd{fd, POLLOUT, 0};
            if (::poll(&pfd, 1, static_cast<int>(timeout.count())) == 1)
            {
                int soError = 0;
                socklen_t len = sizeof(soError);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len);
                rc = soError == 0 ? 0 : -1;
                errno = soError;
            }
        }
        if (rc == 0)
        {
            sockaddr_storage addr{};
            std::memcpy(&addr, p->ai_addr, std::min(sizeof(addr), static_cast<size_t>(p->ai_addrlen)));
            freeaddrinfo(ai);
            return std::make_unique<TcpStreamPosix>(fd, toEndpoint(addr));
        }
        error = "cannot connect to " + host + ":" + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
    }
    freeaddrinfo(ai);
    if (error.empty())
        error = "cannot connect to " + host + ":" + std::to_string(port);
    return nullptr;
}

bool sendUdp(const std::string& address, uint16_t port, std::string_view data, std::string& error)
{
    auto ai = resolve(address, port, SOCK_DGRAM, false, error);
    if (!ai)
        return false;
    auto fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    int broadcast = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    auto n = ::sendto(fd, data.data(), data.size(), 0, ai->ai_addr, ai->ai_addrlen);
    if (n < 0)
        error = std::strerror(errno);
    ::close(fd);
    freeaddrinfo(ai);
    return n >= 0;
}

} // namespace dv::platform
