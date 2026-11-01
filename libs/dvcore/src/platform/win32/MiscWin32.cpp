#include "dvcore/Encoding.h"
#include "dvcore/Platform.h"

#include <windows.h>
#include <psapi.h>
#include <shlobj.h>
#include <io.h>

#include <charconv>
#include <cwctype>

namespace dv::platform {

std::wstring utf8ToWide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    auto len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), len);
    return result;
}

std::string wideToUtf8(std::wstring_view wide)
{
    if (wide.empty())
        return {};
    auto len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::string win32ErrorMessage(DWORD error)
{
    wchar_t* text = nullptr;
    auto len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
    if (len == 0 || !text)
        return "error " + std::to_string(error);
    std::wstring w(text, len);
    LocalFree(text);
    while (!w.empty() && std::iswspace(w.back()))
        w.pop_back();
    return wideToUtf8(w);
}

std::string lastErrorMessage()
{
    return win32ErrorMessage(GetLastError());
}

std::string processName(uint32_t pid)
{
    auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle)
        return {};
    wchar_t buffer[MAX_PATH * 2];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::string result;
    if (QueryFullProcessImageNameW(handle, 0, buffer, &size))
        result = wideToUtf8(std::filesystem::path(std::wstring_view(buffer, size)).filename().wstring());
    CloseHandle(handle);
    return result;
}

uint32_t currentProcessId()
{
    return GetCurrentProcessId();
}

std::filesystem::path configDirectory()
{
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &path)))
        result = std::filesystem::path(path) / L"DebugVision";
    CoTaskMemFree(path);
    return result;
}

uint64_t privateMemoryUsage()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return 0;
    return pmc.PrivateUsage;
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    return wideToUtf8(path.native());
}

std::filesystem::path utf8ToPath(std::string_view utf8)
{
    return std::filesystem::path(utf8ToWide(utf8));
}

namespace {

HANDLE stdHandle(int fd)
{
    switch (fd)
    {
    case 0: return GetStdHandle(STD_INPUT_HANDLE);
    case 1: return GetStdHandle(STD_OUTPUT_HANDLE);
    case 2: return GetStdHandle(STD_ERROR_HANDLE);
    default: return reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    }
}

} // namespace

bool isPipe(int fd)
{
    return GetFileType(stdHandle(fd)) == FILE_TYPE_PIPE;
}

bool isRegularFile(int fd)
{
    return GetFileType(stdHandle(fd)) == FILE_TYPE_DISK;
}

bool isConsole(int fd)
{
    DWORD mode = 0;
    return GetConsoleMode(stdHandle(fd), &mode) != 0;
}

void writeConsole(int fd, std::string_view utf8)
{
    auto handle = stdHandle(fd);
    DWORD written = 0;
    if (isConsole(fd))
    {
        auto wide = utf8ToWide(utf8);
        WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
    }
    else
    {
        WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

} // namespace dv::platform

namespace dv {

namespace {

class Win32LegacyCodec : public LegacyCodec
{
public:
    explicit Win32LegacyCodec(UINT codePage) :
        m_codePage(codePage)
    {
    }

    std::string name() const override { return "cp" + std::to_string(m_codePage); }

    void toUtf8(std::string_view text, std::string& out) override
    {
        if (text.empty())
            return;
        auto wlen = MultiByteToWideChar(m_codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (wlen <= 0)
        {
            appendSanitizedUtf8(text, out);
            return;
        }
        m_wide.resize(static_cast<size_t>(wlen));
        MultiByteToWideChar(m_codePage, 0, text.data(), static_cast<int>(text.size()), m_wide.data(), wlen);
        auto len = WideCharToMultiByte(CP_UTF8, 0, m_wide.data(), wlen, nullptr, 0, nullptr, nullptr);
        auto offset = out.size();
        out.resize(offset + static_cast<size_t>(len));
        WideCharToMultiByte(CP_UTF8, 0, m_wide.data(), wlen, out.data() + offset, len, nullptr, nullptr);
    }

private:
    UINT m_codePage;
    std::wstring m_wide;
};

UINT parseCodePage(std::string_view charset)
{
    if (charset.empty() || charset == "system" || charset == "ansi")
        return GetACP();
    if (charset == "oem")
        return GetOEMCP();
    for (std::string_view prefix : {"windows-", "cp", "CP", "Windows-"})
    {
        if (charset.substr(0, prefix.size()) == prefix)
        {
            charset.remove_prefix(prefix.size());
            break;
        }
    }
    if (charset == "iso-8859-1" || charset == "latin1")
        return 28591;
    unsigned value = 0;
    auto [ptr, ec] = std::from_chars(charset.data(), charset.data() + charset.size(), value);
    if (ec == std::errc() && ptr == charset.data() + charset.size() && IsValidCodePage(value))
        return value;
    return GetACP();
}

} // namespace

std::unique_ptr<LegacyCodec> LegacyCodec::create(std::string_view charset)
{
    return std::make_unique<Win32LegacyCodec>(parseCodePage(charset));
}

} // namespace dv
