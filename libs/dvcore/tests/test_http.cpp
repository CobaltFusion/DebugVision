// HTTP(S) source: Content-Range parsing, and tailing against a small HTTP server on 127.0.0.1

#include "dvcore/Ingest.h"
#include "dvcore/LineStore.h"
#include "dvcore/Platform.h"

#include "sources/Sources.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>

using namespace dv;
using namespace std::chrono_literals;

namespace {

std::vector<std::string> messages(const LineStore& store)
{
    std::vector<std::string> result;
    store.forEach(store.beginSeq(), store.endSeq(), [&](const LineRef& line) {
        if (store.origins().get(line.record->originId).processName != "[internal]")
            result.emplace_back(line.text);
    });
    return result;
}

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 10s)
{
    auto end = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

SourceStatus statusOf(const Ingest& ingest)
{
    auto sources = ingest.sources();
    return sources.empty() ? SourceStatus{} : sources.front().status;
}

std::string base64(const std::string& text)
{
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < text.size(); i += 3)
    {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(text[i])) << 16;
        if (i + 1 < text.size())
            n |= static_cast<uint32_t>(static_cast<unsigned char>(text[i + 1])) << 8;
        if (i + 2 < text.size())
            n |= static_cast<unsigned char>(text[i + 2]);
        out += digits[(n >> 18) & 63];
        out += digits[(n >> 12) & 63];
        out += i + 1 < text.size() ? digits[(n >> 6) & 63] : '=';
        out += i + 2 < text.size() ? digits[n & 63] : '=';
    }
    return out;
}

// Serves 'content' at every path, one request per connection; answers Range requests with 206/416 when 'ranges' is set.
// Also works as an HTTP proxy: a proxy request ("GET http://host/path") is answered the same way.
class TestHttpServer
{
public:
    explicit TestHttpServer(bool ranges) :
        m_ranges(ranges)
    {
        std::string error;
        for (uint16_t port = 28180; port < 28280 && !m_listener; ++port)
        {
            m_listener = platform::TcpListener::open("127.0.0.1", port, error);
            m_port = port;
        }
        REQUIRE(m_listener);
        m_thread = std::thread([this] { serve(); });
    }

    ~TestHttpServer()
    {
        m_stop = true;
        m_thread.join();
    }

    std::string url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(m_port) + path; }

    void setContent(std::string content)
    {
        std::lock_guard lock(m_mutex);
        m_content = std::move(content);
    }

    void append(const std::string& data)
    {
        std::lock_guard lock(m_mutex);
        m_content += data;
    }

    void setStatus(int status)
    {
        std::lock_guard lock(m_mutex);
        m_status = status;
    }

    int requests() const { return m_requests; }

    // like a proxy that requires Basic authentication: answers 407 without "Proxy-Authorization: Basic <user:password>"
    void requireProxyAuth(const std::string& userPassword)
    {
        std::lock_guard lock(m_mutex);
        m_proxyAuth = base64(userPassword);
    }

    std::string lastSocksTarget()
    {
        std::lock_guard lock(m_mutex);
        return m_lastSocksTarget;
    }

    std::string lastRequestLine()
    {
        std::lock_guard lock(m_mutex);
        return m_lastRequestLine;
    }

private:
    void serve()
    {
        while (!m_stop)
        {
            auto stream = m_listener->accept();
            if (!stream)
            {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            std::string request;
            auto end = std::chrono::steady_clock::now() + 2s;
            // reads until 'request' has at least 'size' bytes
            auto readAtLeast = [&](size_t size) {
                while (request.size() < size && std::chrono::steady_clock::now() < end && !m_stop)
                {
                    if (stream->read(request) == platform::AsyncReader::Status::Eof)
                        return false;
                    std::this_thread::sleep_for(1ms);
                }
                return request.size() >= size;
            };
            if (!readAtLeast(1))
                continue;
            if (request[0] == '\x05' && !socks5(*stream, request, readAtLeast))
                continue;
            while (request.find("\r\n\r\n") == std::string::npos && std::chrono::steady_clock::now() < end && !m_stop)
            {
                if (stream->read(request) == platform::AsyncReader::Status::Eof)
                    break;
                std::this_thread::sleep_for(1ms);
            }
            ++m_requests;
            stream->write(respond(request));
        }
    }

    // a SOCKS 5 proxy without authentication that "connects" to this server: answers the handshake and removes it
    // from 'request', which then continues with the HTTP request
    template <typename ReadAtLeast>
    bool socks5(platform::TcpStream& stream, std::string& request, ReadAtLeast& readAtLeast)
    {
        if (!readAtLeast(2) || !readAtLeast(2 + static_cast<unsigned char>(request[1])))
            return false;
        request.erase(0, 2 + static_cast<unsigned char>(request[1]));
        stream.write(std::string("\x05\x00", 2)); // no authentication
        if (!readAtLeast(5))
            return false;
        size_t addressSize = request[3] == '\x01' ? 4 : request[3] == '\x04' ? 16 : 1 + static_cast<unsigned char>(request[4]);
        if (!readAtLeast(4 + addressSize + 2))
            return false;
        std::string target = request[3] == '\x03' ? request.substr(5, addressSize - 1) : "(address)";
        auto port = static_cast<unsigned>(static_cast<unsigned char>(request[4 + addressSize])) * 256 + static_cast<unsigned char>(request[5 + addressSize]);
        {
            std::lock_guard lock(m_mutex);
            m_lastSocksTarget = target + ":" + std::to_string(port);
        }
        request.erase(0, 4 + addressSize + 2);
        stream.write(std::string("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10)); // connected
        return true;
    }

    std::string respond(const std::string& request)
    {
        std::lock_guard lock(m_mutex);
        m_lastRequestLine = request.substr(0, request.find("\r\n"));
        if (!m_proxyAuth.empty())
        {
            // header names are case-insensitive, the credentials are not
            std::string lowerRequest = request;
            for (auto& c : lowerRequest)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            constexpr std::string_view header = "\r\nproxy-authorization: basic ";
            auto at = lowerRequest.find(header);
            bool ok = at != std::string::npos && request.compare(at + header.size(), m_proxyAuth.size() + 2, m_proxyAuth + "\r\n") == 0;
            if (!ok)
                return "HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=\"dv\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        }
        if (m_status != 200)
            return "HTTP/1.1 " + std::to_string(m_status) + " Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

        std::string lower = request;
        for (auto& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto range = lower.find("\r\nrange: bytes=");
        if (!m_ranges || range == std::string::npos)
            return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(m_content.size()) + "\r\nConnection: close\r\n\r\n" + m_content;

        auto spec = lower.substr(range + 15, lower.find("\r\n", range + 2) - range - 15);
        auto first = std::stoull(spec.substr(0, spec.find('-')));
        auto last = std::stoull(spec.substr(spec.find('-') + 1));
        auto size = m_content.size();
        if (first >= size)
            return "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */" + std::to_string(size) + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        last = std::min<uint64_t>(last, size - 1);
        auto body = m_content.substr(first, last - first + 1);
        return "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" + std::to_string(size) +
            "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    }

    bool m_ranges;
    std::unique_ptr<platform::TcpListener> m_listener;
    uint16_t m_port = 0;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_requests{0};
    std::mutex m_mutex;
    std::string m_content;
    int m_status = 200;
    std::string m_proxyAuth;
    std::string m_lastRequestLine;
    std::string m_lastSocksTarget;
};

// "direct" by default: the test server must not be reached through a proxy of the system settings
SourceConfig httpSource(const std::string& url, bool tail = true, const std::string& proxy = "direct")
{
    auto config = SourceRegistry::instance().makeDefault("http");
    HttpParams params;
    params.url = url;
    params.tail = tail;
    params.pollMilliseconds = 100;
    params.proxy = proxy;
    config.setParams(params);
    return config;
}

} // namespace

TEST_CASE("Content-Range header parsing")
{
    auto r = parseContentRange("bytes 0-99/1234");
    CHECK(r.first == 0);
    CHECK(r.last == 99);
    CHECK(r.total == 1234);
    r = parseContentRange("bytes */1234");
    CHECK(r.first == -1);
    CHECK(r.total == 1234);
    r = parseContentRange("bytes 5-9/*");
    CHECK(r.first == 5);
    CHECK(r.total == -1);
    CHECK(parseContentRange("").total == -1);
    CHECK(parseContentRange("items 0-1/2").first == -1);
}

TEST_CASE("http source: url validation")
{
    CHECK_NOTHROW(SourceRegistry::instance().parseSpec("http:url=https://example.com/app.log"));
    CHECK_THROWS(SourceRegistry::instance().parseSpec("http:url=ftp://example.com/app.log"));
}

TEST_CASE("http source: tail with range requests, partial line and truncation")
{
    TestHttpServer server(true);
    server.setContent("line 1\nline 2\npart");

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(httpSource(server.url("/logs/app.log")));

    CHECK(waitFor([&] { return messages(store).size() == 2; }));
    CHECK(statusOf(ingest).state == SourceState::Running);
    server.append("ial\nline 4\n");
    CHECK(waitFor([&] { return messages(store).size() == 4; }));
    CHECK(messages(store)[2] == "partial");

    // truncation: the file is read again from the start
    server.setContent("new\n");
    CHECK(waitFor([&] { return messages(store).size() == 5; }));
    CHECK(messages(store).back() == "new");
    ingest.stop();
}

TEST_CASE("http source: a file larger than one request is read in chunks")
{
    TestHttpServer server(true);
    std::string content;
    for (int i = 0; i < 100000; ++i)
        content += "line " + std::to_string(i) + " with some padding to make it longer\n";
    REQUIRE(content.size() > 2 * 1024 * 1024);
    server.setContent(content);

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(httpSource(server.url("/big.log")));
    CHECK(waitFor([&] { return messages(store).size() == 100000; }));
    auto lines = messages(store);
    CHECK(lines.front() == "line 0 with some padding to make it longer");
    CHECK(lines.back() == "line 99999 with some padding to make it longer");
    ingest.stop();
}

TEST_CASE("http source: server without range support")
{
    TestHttpServer server(false);
    server.setContent("a\nb\n");

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(httpSource(server.url("/plain.log")));
    CHECK(waitFor([&] { return messages(store).size() == 2; }));
    server.append("c\n");
    CHECK(waitFor([&] { return messages(store).size() == 3; }));
    std::this_thread::sleep_for(300ms); // a few more polls: nothing is added twice
    CHECK(messages(store) == std::vector<std::string>{"a", "b", "c"});
    ingest.stop();
}

TEST_CASE("http source: errors are retried, loading without tail stops at the end")
{
    TestHttpServer server(true);
    server.setStatus(404);

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(httpSource(server.url("/missing.log")));
    CHECK(waitFor([&] { return statusOf(ingest).state == SourceState::Waiting; }));
    CHECK(statusOf(ingest).text.find("404") != std::string::npos);

    server.setStatus(200);
    server.setContent("found\n");
    CHECK(waitFor([&] { return messages(store).size() == 1; }));
    ingest.stop();

    LineStore loaded;
    Ingest once(loaded);
    once.start();
    once.addSource(httpSource(server.url("/once.log"), false));
    CHECK(waitFor([&] { return statusOf(once).state == SourceState::Stopped && messages(loaded).size() == 1; }));
    auto requests = server.requests();
    std::this_thread::sleep_for(300ms);
    CHECK(server.requests() == requests); // no more polling
    once.stop();
}

TEST_CASE("proxy settings")
{
    using platform::HttpProxy;
    HttpProxy proxy;
    std::string error;
    REQUIRE(platform::parseHttpProxy("proxy.example.com:8080", proxy, error));
    CHECK(proxy.scheme == "http");
    CHECK(proxy.host == "proxy.example.com");
    CHECK(proxy.port == 8080);
    REQUIRE(platform::parseHttpProxy("HTTP://us%40er:p%3Ass@[::1]:3128/", proxy, error));
    CHECK(proxy.scheme == "http");
    CHECK(proxy.user == "us@er");
    CHECK(proxy.password == "p:ss");
    CHECK(proxy.host == "::1");
    CHECK(proxy.port == 3128);
    CHECK_FALSE(platform::parseHttpProxy("http://proxy.example.com", proxy, error));
    CHECK(error.find("port") != std::string::npos);
    CHECK_FALSE(platform::parseHttpProxy("http://:8080", proxy, error));
    CHECK_FALSE(platform::parseHttpProxy("ftp://proxy:21", proxy, error));

    CHECK(platform::validateHttpProxy("").empty());
    CHECK(platform::validateHttpProxy("system").empty());
    CHECK(platform::validateHttpProxy("direct").empty());
    CHECK(platform::validateHttpProxy("socks5h://127.0.0.1:1080").empty());
    CHECK(platform::validateHttpProxy("socks4a://127.0.0.1:1080").empty());
    CHECK(platform::validateHttpProxy("https://proxy:443").empty());
}

TEST_CASE("http source: the default proxy is filled in for sources without one")
{
    std::vector<SourceConfig> configs{httpSource("http://a/x.log", true, ""), httpSource("http://b/y.log", true, "direct"), SourceRegistry::instance().makeDefault("udp")};
    applyHttpProxyDefault(configs, "http://proxy:8080");
    CHECK(configs[0].getParams<HttpParams>().proxy == "http://proxy:8080");
    CHECK(configs[1].getParams<HttpParams>().proxy == "direct");
    CHECK(configs[2].params.find("proxy") == configs[2].params.end());
    CHECK_THROWS(SourceRegistry::instance().parseSpec("http:url=http://a/x.log,proxy=http://proxy"));
}

TEST_CASE("http source: through an http proxy, with proxy authentication")
{
    TestHttpServer proxy(true);
    proxy.setContent("via proxy\n");
    // the host does not exist: only the proxy can answer
    const std::string url = "http://dv-test.invalid/logs/app.log";

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(httpSource(url, true, proxy.url("")));
    CHECK(waitFor([&] { return messages(store).size() == 1; }));
    CHECK(proxy.lastRequestLine().find("GET " + url + " ") == 0);
    CHECK(statusOf(ingest).text.find("via proxy http://127.0.0.1:") != std::string::npos);
    ingest.stop();

    proxy.requireProxyAuth("dv:secret");
    LineStore rejected;
    Ingest noCredentials(rejected);
    noCredentials.start();
    noCredentials.addSource(httpSource(url, true, proxy.url("")));
    CHECK(waitFor([&] { return statusOf(noCredentials).state == SourceState::Waiting; }));
    CHECK(statusOf(noCredentials).text.find("user name") != std::string::npos);
    noCredentials.stop();

    LineStore accepted;
    Ingest withCredentials(accepted);
    withCredentials.start();
    auto credentialsProxy = proxy.url("");
    credentialsProxy.insert(credentialsProxy.find("//") + 2, "dv:secret@");
    withCredentials.addSource(httpSource(url, true, credentialsProxy));
    CHECK(waitFor([&] { return messages(accepted).size() == 1; }));
    CHECK(statusOf(withCredentials).text.find("secret") == std::string::npos); // the password is not shown
    withCredentials.stop();
}

TEST_CASE("http source: through a SOCKS 5 proxy that resolves the host name")
{
    TestHttpServer proxy(true);
    proxy.setContent("via socks\n");
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    auto socks = proxy.url("");
    socks.replace(0, socks.find("://"), "socks5h");
    // the host does not exist: only the proxy can reach it
    ingest.addSource(httpSource("http://dv-test.invalid:8080/app.log", true, socks));
    CHECK(waitFor([&] { return messages(store).size() == 1; }));
    CHECK(proxy.lastSocksTarget() == "dv-test.invalid:8080");
    CHECK(statusOf(ingest).text.find("via proxy socks5h://127.0.0.1:") != std::string::npos);
    ingest.stop();
}

TEST_CASE("local SOCKS proxies are detected, closed ports are skipped")
{
    auto portOf = [](const TestHttpServer& server) {
        auto url = server.url("");
        return static_cast<uint16_t>(std::stoi(url.substr(url.rfind(':') + 1)));
    };
    TestHttpServer socks(true);
    auto port = portOf(socks);
    uint16_t closedPort = 0;
    {
        TestHttpServer stopped(true); // created while 'socks' runs: a different port, closed again below
        closedPort = portOf(stopped);
    }
    REQUIRE(closedPort != port);

    auto start = std::chrono::steady_clock::now();
    auto found = platform::detectLocalSocksProxies({closedPort, port}, 300ms);
    CHECK(std::chrono::steady_clock::now() - start < 2s); // the probes run in parallel
    REQUIRE(found.size() == 1);
    CHECK(found[0].proxy == "socks5h://127.0.0.1:" + std::to_string(port));
    CHECK(found[0].note.empty());
    CHECK(socks.requests() == 0); // only the greeting was sent, no request
}
