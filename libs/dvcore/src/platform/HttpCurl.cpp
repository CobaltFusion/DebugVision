// HttpClient with libcurl (the easy interface, called from a helper thread), on all platforms (see cmake/Curl.cmake).
// Proxies: http, https and SOCKS proxy URLs are passed on to libcurl. Without a proxy setting ("system"), the proxy
// settings of Windows are used there, and libcurl reads the http_proxy/https_proxy/all_proxy environment variables.

#include "dvcore/Platform.h"

#include <curl/curl.h>

#include <atomic>
#include <cctype>
#include <mutex>
#include <optional>

namespace dv::platform {

#ifdef _WIN32
std::string windowsSystemProxy(const std::string& url); // SystemProxyWin32.cpp
#endif

namespace {

size_t onBody(char* data, size_t size, size_t count, void* user)
{
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

size_t onHeader(char* data, size_t size, size_t count, void* user)
{
    std::string_view line(data, size * count);
    constexpr std::string_view name = "content-range:";
    if (line.size() > name.size())
    {
        bool match = true;
        for (size_t i = 0; i < name.size() && match; ++i)
            match = static_cast<char>(std::tolower(static_cast<unsigned char>(line[i]))) == name[i];
        if (match)
        {
            auto value = line.substr(name.size());
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.remove_prefix(1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
                value.remove_suffix(1);
            *static_cast<std::string*>(user) = std::string(value);
        }
    }
    return size * count;
}

class CurlClient : public HttpClient
{
public:
    // proxy: "" = the system settings, "direct" = no proxy, otherwise a validated proxy URL
    CurlClient(CURL* curl, std::string proxy) :
        m_curl(curl),
        m_proxy(std::move(proxy))
    {
    }

    ~CurlClient() override { curl_easy_cleanup(m_curl); }

    HttpResponse get(const std::string& url, uint64_t first, uint64_t last) override
    {
        HttpResponse response;
        if (m_cancelled)
        {
            response.error = "cancelled";
            return response;
        }
        auto range = std::to_string(first) + "-" + std::to_string(last);
        curl_easy_reset(m_curl);
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_RANGE, range.c_str());
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "DebugVision");
        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 10L);
        auto proxy = proxyFor(url);
        if (proxy == "direct")
            curl_easy_setopt(m_curl, CURLOPT_PROXY, ""); // also ignores the proxy environment variables
        else if (!proxy.empty())
            curl_easy_setopt(m_curl, CURLOPT_PROXY, proxy.c_str());
        // whatever the proxy asks for: basic, digest, and NTLM or Negotiate with the credentials of the proxy URL
        curl_easy_setopt(m_curl, CURLOPT_PROXYAUTH, static_cast<long>(CURLAUTH_ANY));
#ifdef _WIN32
        // Schannel: do not fail when the certificate revocation server cannot be reached (as WinHTTP and browsers)
        curl_easy_setopt(m_curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
        curl_easy_setopt(m_curl, CURLOPT_PROXY_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif
        // a stalled server makes the request fail instead of hanging
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, &onBody);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, &onHeader);
        curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, &response.contentRange);
        // the progress callback is how cancel() aborts a running transfer
        curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(m_curl, CURLOPT_XFERINFOFUNCTION, &onProgress);
        curl_easy_setopt(m_curl, CURLOPT_XFERINFODATA, this);

        auto result = curl_easy_perform(m_curl);
        if (result != CURLE_OK)
        {
            response.error = m_cancelled ? "cancelled" : curl_easy_strerror(result);
            response.body.clear();
            return response;
        }
        long status = 0;
        curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &status);
        response.status = static_cast<int>(status);
        if (status == 407)
            response = HttpResponse{0, "the proxy requires a user name and password, or rejected them (proxy=http://user:password@host:port)", {}, {}};
        return response;
    }

    void cancel() override { m_cancelled = true; }

private:
    std::string proxyFor(const std::string& url)
    {
#ifdef _WIN32
        if (m_proxy.empty())
        {
            // looked up once: a setup script (PAC) or automatic detection can take a while
            if (!m_systemProxy || m_systemProxyUrl != url)
            {
                m_systemProxy = windowsSystemProxy(url);
                m_systemProxyUrl = url;
            }
            return *m_systemProxy;
        }
#else
        (void)url;
#endif
        return m_proxy;
    }

    static int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return static_cast<CurlClient*>(user)->m_cancelled ? 1 : 0;
    }

    CURL* m_curl;
    std::string m_proxy;
#ifdef _WIN32
    std::optional<std::string> m_systemProxy;
    std::string m_systemProxyUrl;
#endif
    std::atomic<bool> m_cancelled{false};
};

} // namespace

std::unique_ptr<HttpClient> HttpClient::create(const std::string& proxy, std::string& error)
{
    std::string curlProxy;
    if (proxy == "direct")
        curlProxy = "direct";
    else if (!proxy.empty() && proxy != "system")
    {
        HttpProxy parsed;
        if (!parseHttpProxy(proxy, parsed, error))
            return nullptr;
        curlProxy = proxy.find("://") == std::string::npos ? "http://" + proxy : proxy;
    }
    static std::once_flag initialized;
    std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        error = "cannot initialize libcurl";
        return nullptr;
    }
    return std::make_unique<CurlClient>(curl, std::move(curlProxy));
}

} // namespace dv::platform
