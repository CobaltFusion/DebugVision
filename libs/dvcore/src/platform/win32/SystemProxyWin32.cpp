// The Windows proxy settings (Settings > Network & internet > Proxy) for one URL, for libcurl, which does not read them:
// automatic detection (WPAD) and setup scripts (PAC) through WinHttpGetProxyForUrl, otherwise the manual proxy server
// with its exceptions ("bypass" list, "<local>" for host names without a dot).

#include <windows.h>
#include <winhttp.h>

#include <cwctype>
#include <string>
#include <vector>

namespace dv::platform {

std::wstring utf8ToWide(std::string_view utf8); // MiscWin32.cpp
std::string wideToUtf8(std::wstring_view wide);

namespace {

std::wstring lower(std::wstring s)
{
    for (auto& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

// "a:1; http=b:2 https=c:3" -> {"a:1", "http=b:2", "https=c:3"}
std::vector<std::wstring> splitList(const std::wstring& list)
{
    std::vector<std::wstring> items;
    std::wstring item;
    for (auto c : list + L";")
    {
        if (c == L';' || std::iswspace(c))
        {
            if (!item.empty())
                items.push_back(item);
            item.clear();
        }
        else
            item.push_back(c);
    }
    return items;
}

bool wildcardMatch(const std::wstring& pattern, const std::wstring& text)
{
    size_t p = 0, t = 0, star = std::wstring::npos, mark = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == text[t] || pattern[p] == L'?'))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == L'*')
        {
            star = p++;
            mark = t;
        }
        else if (star != std::wstring::npos)
        {
            p = star + 1;
            t = ++mark;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == L'*')
        ++p;
    return p == pattern.size();
}

bool bypassed(const std::wstring& host, const std::wstring& bypassList)
{
    for (auto& entry : splitList(lower(bypassList)))
    {
        if (entry == L"<local>" ? host.find(L'.') == std::wstring::npos : wildcardMatch(entry, host))
            return true;
        // entries can have a scheme ("https://*.example.com") or a port: compare the host part as well
        auto schemeEnd = entry.find(L"://");
        auto hostPattern = schemeEnd == std::wstring::npos ? entry : entry.substr(schemeEnd + 3);
        if (hostPattern != entry && wildcardMatch(hostPattern, host))
            return true;
    }
    return false;
}

// picks the proxy for 'scheme' from a proxy list, as a libcurl proxy URL; "" = direct
std::string pickProxy(const std::wstring& list, const std::wstring& scheme)
{
    std::wstring generic, socks;
    for (auto& item : splitList(list))
    {
        auto eq = item.find(L'=');
        if (eq == std::wstring::npos)
        {
            if (generic.empty())
                generic = item;
            continue;
        }
        auto key = lower(item.substr(0, eq));
        auto value = item.substr(eq + 1);
        if (key == scheme)
            return wideToUtf8(value.find(L"://") == std::wstring::npos ? L"http://" + value : value);
        if (key == L"socks" && socks.empty())
            socks = value;
    }
    if (!generic.empty())
        return wideToUtf8(generic.find(L"://") == std::wstring::npos ? L"http://" + generic : generic);
    if (!socks.empty())
        return wideToUtf8(L"socks4://" + socks); // the "socks=" entry of Windows is a SOCKS 4 proxy
    return {};
}

} // namespace

// returns the libcurl proxy URL for 'url', or "" when it is reached directly
std::string windowsSystemProxy(const std::string& url)
{
    auto wideUrl = utf8ToWide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts))
        return {};
    auto scheme = lower(std::wstring(parts.lpszScheme, parts.dwSchemeLength));
    auto host = lower(std::wstring(parts.lpszHostName, parts.dwHostNameLength));

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config))
        return {};
    std::wstring autoConfigUrl = config.lpszAutoConfigUrl ? config.lpszAutoConfigUrl : L"";
    std::wstring manualProxy = config.lpszProxy ? config.lpszProxy : L"";
    std::wstring bypassList = config.lpszProxyBypass ? config.lpszProxyBypass : L"";
    bool autoDetect = config.fAutoDetect;
    GlobalFree(config.lpszAutoConfigUrl);
    GlobalFree(config.lpszProxy);
    GlobalFree(config.lpszProxyBypass);

    if (autoDetect || !autoConfigUrl.empty())
    {
        HINTERNET session = WinHttpOpen(L"DebugVision", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session)
        {
            WINHTTP_AUTOPROXY_OPTIONS options{};
            if (!autoConfigUrl.empty())
            {
                options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
                options.lpszAutoConfigUrl = autoConfigUrl.c_str();
            }
            if (autoDetect)
            {
                options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
                options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }
            options.fAutoLogonIfChallenged = TRUE;
            WINHTTP_PROXY_INFO info{};
            bool found = WinHttpGetProxyForUrl(session, wideUrl.c_str(), &options, &info);
            std::string proxy;
            if (found && info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && info.lpszProxy)
                proxy = pickProxy(info.lpszProxy, scheme);
            if (found)
            {
                GlobalFree(info.lpszProxy);
                GlobalFree(info.lpszProxyBypass);
            }
            WinHttpCloseHandle(session);
            if (found)
                return proxy;
        }
        // no setup script found: use the manual settings, as Windows does
    }

    if (manualProxy.empty() || bypassed(host, bypassList))
        return {};
    return pickProxy(manualProxy, scheme);
}

} // namespace dv::platform
