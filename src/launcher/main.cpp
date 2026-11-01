// Self-contained debugvision.exe: unpacks the application and its DLLs, embedded as compressed resources (see CMakeLists.txt),
// into a new directory under %TEMP%\DebugVision, runs the application from there with the same command line and
// standard handles, and removes the directory again when the application exits.

#include <windows.h>
#include <compressapi.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "payload.h"

namespace fs = std::filesystem;

namespace {

// kept open without sharing while the application runs, so other instances do not remove the directory
constexpr wchar_t lockFileName[] = L".lock";

std::wstring lastErrorText()
{
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, GetLastError(), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : L"unknown error";
    LocalFree(buffer);
    return text;
}

int fail(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), L"DebugVision", MB_OK | MB_ICONERROR);
    return 1;
}

// removes the directories of earlier runs that were left behind because the launcher was killed or the machine crashed
void removeStaleDirectories(const fs::path& root)
{
    try
    {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(root, ec))
        {
            if (!entry.is_directory(ec))
                continue;
            auto lock = entry.path() / lockFileName;
            if (!DeleteFileW(lock.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
                continue; // still in use
            fs::remove_all(entry.path(), ec);
        }
    }
    catch (const fs::filesystem_error&)
    {
    }
}

// creates a new directory for this run and its lock file; another instance cleaning up can remove the directory
// between both steps, then creating the lock fails and a new directory is tried
HANDLE createRunDirectory(const fs::path& root, fs::path& dir)
{
    for (unsigned attempt = 0; attempt < 10; ++attempt)
    {
        dir = root / (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64() + attempt));
        std::error_code ec;
        if (!fs::create_directories(dir, ec))
            continue;
        HANDLE lock = CreateFileW((dir / lockFileName).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (lock != INVALID_HANDLE_VALUE)
            return lock;
    }
    return INVALID_HANDLE_VALUE;
}

// the embedded files are compressed with XPRESS Huffman by dv_compress (compress.cpp)
bool extract(const fs::path& dir, std::wstring& error)
{
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &decompressor))
    {
        error = L"cannot create a decompressor: " + lastErrorText();
        return false;
    }
    std::vector<char> buffer;
    bool ok = true;
    for (const auto& file : payload)
    {
        auto path = dir / file.path;
        HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(file.id), L"DVFILE");
        HGLOBAL data = resource ? LoadResource(nullptr, resource) : nullptr;
        if (!data)
        {
            error = L"missing embedded file " + path.filename().wstring();
            ok = false;
            break;
        }
        const void* compressed = LockResource(data);
        DWORD compressedSize = SizeofResource(nullptr, resource);
        SIZE_T size = 0;
        Decompress(decompressor, compressed, compressedSize, nullptr, 0, &size);
        buffer.resize(size);
        if (!Decompress(decompressor, compressed, compressedSize, buffer.data(), buffer.size(), &size))
        {
            error = L"cannot decompress " + path.filename().wstring() + L": " + lastErrorText();
            ok = false;
            break;
        }

        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = L"cannot create " + path.wstring() + L": " + lastErrorText();
            ok = false;
            break;
        }
        DWORD written = 0;
        ok = WriteFile(handle, buffer.data(), static_cast<DWORD>(size), &written, nullptr) && written == size;
        if (!ok)
            error = L"cannot write " + path.wstring() + L": " + lastErrorText();
        CloseHandle(handle);
        if (!ok)
            break;
    }
    CloseDecompressor(decompressor);
    return ok;
}

// the command line without argv[0], which has its own parsing rules: up to the closing quote, or the first space or tab
const wchar_t* arguments(const wchar_t* commandLine)
{
    if (*commandLine == L'"')
    {
        ++commandLine;
        while (*commandLine && *commandLine != L'"')
            ++commandLine;
        if (*commandLine)
            ++commandLine;
    }
    else
    {
        while (*commandLine && *commandLine != L' ' && *commandLine != L'\t')
            ++commandLine;
    }
    return commandLine;
}

// passes the standard handles on, so piping into debugvision.exe and --help keep working
bool inheritStandardHandles(STARTUPINFOW& startup)
{
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    bool any = false;
    for (HANDLE handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError})
    {
        if (handle && handle != INVALID_HANDLE_VALUE && SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            any = true;
    }
    if (any)
        startup.dwFlags |= STARTF_USESTDHANDLES;
    return any;
}

void removeDirectory(const fs::path& dir)
{
    // the loader can still hold the DLLs briefly after the process has exited
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
        if (!ec)
            return;
        Sleep(50);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    wchar_t tempPath[MAX_PATH + 1];
    if (!GetTempPathW(MAX_PATH + 1, tempPath))
        return fail(L"cannot determine the temporary directory: " + lastErrorText());
    auto root = fs::path(tempPath) / L"DebugVision";
    removeStaleDirectories(root);

    fs::path dir;
    HANDLE lock = createRunDirectory(root, dir);
    if (lock == INVALID_HANDLE_VALUE)
        return fail(L"cannot create a directory in " + root.wstring() + L": " + lastErrorText());

    std::wstring error;
    if (!extract(dir, error))
    {
        CloseHandle(lock);
        removeDirectory(dir);
        return fail(error);
    }

    auto exe = dir / L"debugvision.exe";
    std::wstring commandLine = L"\"" + exe.wstring() + L"\"" + arguments(GetCommandLineW());

    STARTUPINFOW own{};
    own.cb = sizeof(own);
    GetStartupInfoW(&own);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = own.dwFlags & STARTF_USESHOWWINDOW;
    startup.wShowWindow = own.wShowWindow;
    bool inherit = inheritStandardHandles(startup);

    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, inherit, 0, nullptr, nullptr, &startup, &process))
    {
        error = L"cannot start " + exe.wstring() + L": " + lastErrorText();
        CloseHandle(lock);
        removeDirectory(dir);
        return fail(error);
    }
    AllowSetForegroundWindow(process.dwProcessId);
    CloseHandle(process.hThread);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);

    CloseHandle(lock);
    removeDirectory(dir);
    return static_cast<int>(exitCode);
}
