#include "dvcore/Platform.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace dv::platform {

std::wstring utf8ToWide(std::string_view utf8); // MiscWin32.cpp
std::string wideToUtf8(std::wstring_view wide);
std::string win32ErrorMessage(DWORD error);

// ---------------------------------------------------------------------------------------------------- File

File::~File()
{
    close();
}

File::File(File&& other) noexcept :
    m_handle(other.m_handle)
{
    other.m_handle = nullptr;
}

File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_handle = other.m_handle;
        other.m_handle = nullptr;
    }
    return *this;
}

std::optional<File> File::open(const std::filesystem::path& path, std::string& error)
{
    auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = win32ErrorMessage(GetLastError());
        return std::nullopt;
    }
    File file;
    file.m_handle = handle;
    return file;
}

namespace {

std::optional<FileId> idOfHandle(HANDLE handle)
{
    FILE_ID_INFO info{};
    if (GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info)))
    {
        FileId id;
        id.volume = info.VolumeSerialNumber;
        std::memcpy(&id.low, info.FileId.Identifier, 8);
        std::memcpy(&id.high, info.FileId.Identifier + 8, 8);
        return id;
    }
    BY_HANDLE_FILE_INFORMATION bhfi{};
    if (GetFileInformationByHandle(handle, &bhfi))
    {
        FileId id;
        id.volume = bhfi.dwVolumeSerialNumber;
        id.low = (static_cast<uint64_t>(bhfi.nFileIndexHigh) << 32) | bhfi.nFileIndexLow;
        return id;
    }
    return std::nullopt;
}

} // namespace

std::optional<FileId> File::idOf(const std::filesystem::path& path)
{
    auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;
    auto id = idOfHandle(handle);
    CloseHandle(handle);
    return id;
}

bool File::isOpen() const
{
    return m_handle != nullptr;
}

void File::close()
{
    if (m_handle)
        CloseHandle(m_handle);
    m_handle = nullptr;
}

int64_t File::read(void* buffer, size_t size)
{
    DWORD read = 0;
    auto request = static_cast<DWORD>(std::min<size_t>(size, 0x40000000));
    if (!ReadFile(m_handle, buffer, request, &read, nullptr))
        return -1;
    return read;
}

bool File::seek(uint64_t offset)
{
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(m_handle, li, nullptr, FILE_BEGIN) != 0;
}

int64_t File::size() const
{
    LARGE_INTEGER li;
    if (!GetFileSizeEx(m_handle, &li))
        return -1;
    return li.QuadPart;
}

std::optional<FileId> File::id() const
{
    return idOfHandle(m_handle);
}

// ---------------------------------------------------------------------------------------------------- AsyncReader

class AsyncReader::Impl
{
public:
    explicit Impl(HANDLE handle) :
        m_handle(handle)
    {
        m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        issueRead();
    }

    ~Impl()
    {
        if (m_pending)
        {
            CancelIoEx(m_handle, &m_overlapped);
            DWORD n = 0;
            GetOverlappedResult(m_handle, &m_overlapped, &n, TRUE);
        }
        CloseHandle(m_handle);
        CloseHandle(m_event);
    }

    HANDLE waitHandle() const { return m_event; }

    Status read(std::string& out)
    {
        while (!m_eof)
        {
            if (m_pending)
            {
                DWORD n = 0;
                if (!GetOverlappedResult(m_handle, &m_overlapped, &n, FALSE))
                {
                    auto error = GetLastError();
                    if (error == ERROR_IO_INCOMPLETE)
                        return Status::Ok;
                    m_pending = false;
                    m_eof = true;
                    SetEvent(m_event); // keep signaled so the owner notices the end
                    break;
                }
                m_pending = false;
                out.append(m_buffer, n);
            }
            if (!issueRead())
                break;
        }
        return m_eof ? Status::Eof : Status::Ok;
    }

private:
    // returns false at end of stream
    bool issueRead()
    {
        ResetEvent(m_event);
        m_overlapped = OVERLAPPED{};
        m_overlapped.hEvent = m_event;
        if (ReadFile(m_handle, m_buffer, sizeof(m_buffer), nullptr, &m_overlapped) || GetLastError() == ERROR_IO_PENDING)
        {
            m_pending = true;
            return true;
        }
        m_eof = true;
        SetEvent(m_event);
        return false;
    }

    HANDLE m_handle;
    HANDLE m_event;
    OVERLAPPED m_overlapped{};
    bool m_pending = false;
    bool m_eof = false;
    char m_buffer[64 * 1024];
};

AsyncReader::AsyncReader(NativeHandle handle) :
    m_impl(std::make_unique<Impl>(static_cast<HANDLE>(handle)))
{
}

AsyncReader::~AsyncReader() = default;

NativeHandle AsyncReader::waitHandle() const
{
    return m_impl->waitHandle();
}

AsyncReader::Status AsyncReader::read(std::string& out)
{
    return m_impl->read(out);
}

// ---------------------------------------------------------------------------------------------------- ChildProcess

namespace {

// quoting according to the rules of CommandLineToArgvW / the MSVC runtime
void appendQuoted(std::wstring& cmd, const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
    {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (size_t i = 0;; ++i)
    {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\')
        {
            ++i;
            ++backslashes;
        }
        if (i == arg.size())
        {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"')
        {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
        }
        else
        {
            cmd.append(backslashes, L'\\');
            cmd += arg[i];
        }
    }
    cmd += L'"';
}

// creates an overlapped, non-inheritable read end and an inheritable synchronous write end
bool createPipe(HANDLE& readEnd, HANDLE& writeEnd, std::string& error)
{
    static std::atomic<unsigned> counter{0};
    auto name = L"\\\\.\\pipe\\debugvision-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    readEnd = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 64 * 1024, 0, nullptr);
    if (readEnd == INVALID_HANDLE_VALUE)
    {
        error = win32ErrorMessage(GetLastError());
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    writeEnd = CreateFileW(name.c_str(), GENERIC_WRITE, 0, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (writeEnd == INVALID_HANDLE_VALUE)
    {
        error = win32ErrorMessage(GetLastError());
        CloseHandle(readEnd);
        return false;
    }
    return true;
}

std::string describeExitCode(DWORD code)
{
    char buf[64];
    if (code >= 0xC0000000u)
    {
        std::snprintf(buf, sizeof(buf), "exit code 0x%08lX", static_cast<unsigned long>(code));
        std::string result = buf;
        wchar_t* text = nullptr;
        auto ntdll = GetModuleHandleW(L"ntdll.dll");
        auto len = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, ntdll, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        if (len > 0 && text)
        {
            std::wstring w(text, len);
            LocalFree(text);
            while (!w.empty() && std::iswspace(w.back()))
                w.pop_back();
            result += " (" + wideToUtf8(w) + ")";
        }
        return result;
    }
    std::snprintf(buf, sizeof(buf), "exit code %lu", static_cast<unsigned long>(code));
    return buf;
}

class ChildProcessWin32 : public ChildProcess
{
public:
    ChildProcessWin32(PROCESS_INFORMATION pi, HANDLE stdoutRead, HANDLE stderrRead) :
        m_process(pi.hProcess),
        m_pid(pi.dwProcessId),
        m_stdout(stdoutRead),
        m_stderr(stderrRead)
    {
        CloseHandle(pi.hThread);
    }

    ~ChildProcessWin32() override
    {
        CloseHandle(m_process);
    }

    uint32_t pid() const override { return m_pid; }
    AsyncReader& stdoutReader() override { return m_stdout; }
    AsyncReader& stderrReader() override { return m_stderr; }
    ProcessRef processRef() const override { return ProcessRef{m_process}; }

    std::string exitDescription() override
    {
        DWORD code = 0;
        if (!GetExitCodeProcess(m_process, &code))
            return "unknown exit code";
        if (code == STILL_ACTIVE)
            return "still running";
        return describeExitCode(code);
    }

    void terminate() override
    {
        TerminateProcess(m_process, 1);
    }

private:
    HANDLE m_process;
    uint32_t m_pid;
    AsyncReader m_stdout;
    AsyncReader m_stderr;
};

std::vector<wchar_t> buildEnvironment(const std::vector<std::pair<std::string, std::string>>& additions)
{
    std::vector<wchar_t> block;
    auto env = GetEnvironmentStringsW();
    std::vector<std::wstring> entries;
    for (auto p = env; *p; p += wcslen(p) + 1)
        entries.emplace_back(p);
    FreeEnvironmentStringsW(env);
    for (auto& [key, value] : additions)
    {
        auto wkey = utf8ToWide(key);
        auto prefix = wkey + L"=";
        std::erase_if(entries, [&](const std::wstring& e) { return _wcsnicmp(e.c_str(), prefix.c_str(), prefix.size()) == 0; });
        entries.push_back(prefix + utf8ToWide(value));
    }
    for (auto& e : entries)
    {
        block.insert(block.end(), e.begin(), e.end());
        block.push_back(0);
    }
    block.push_back(0);
    return block;
}

} // namespace

std::unique_ptr<ChildProcess> ChildProcess::start(const ChildProcessSpec& spec, std::string& error)
{
    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errRead = nullptr;
    HANDLE errWrite = nullptr;
    if (!createPipe(outRead, outWrite, error))
        return nullptr;
    if (!createPipe(errRead, errWrite, error))
    {
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return nullptr;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    auto nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    std::wstring cmd;
    appendQuoted(cmd, utf8ToWide(spec.program));
    for (auto& arg : spec.arguments)
    {
        cmd += L' ';
        appendQuoted(cmd, utf8ToWide(arg));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    std::vector<wchar_t> environment;
    if (!spec.environment.empty())
        environment = buildEnvironment(spec.environment);
    auto cwd = utf8ToWide(spec.workingDirectory);

    PROCESS_INFORMATION pi{};
    auto ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.empty() ? nullptr : environment.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    auto lastError = GetLastError();
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok)
    {
        error = win32ErrorMessage(lastError);
        CloseHandle(outRead);
        CloseHandle(errRead);
        return nullptr;
    }
    return std::make_unique<ChildProcessWin32>(pi, outRead, errRead);
}

} // namespace dv::platform
