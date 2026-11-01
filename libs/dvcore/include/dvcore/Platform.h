#pragma once

// The only OS-specific interface of dvcore; everything above this header is portable C++20.
// Implementations: src/platform/win32, src/platform/linux, src/platform/macos and src/platform/posix (shared POSIX code).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dv::platform {

#ifdef _WIN32
using NativeHandle = void*; // HANDLE: a waitable object
inline const NativeHandle invalidHandle = nullptr;
#else
using NativeHandle = int; // file descriptor
inline constexpr NativeHandle invalidHandle = -1;
#endif

// Identifies a process to wait for: a process HANDLE on Windows, a pid (and pidfd on Linux) on POSIX.
struct ProcessRef
{
#ifdef _WIN32
    void* handle = nullptr;
#else
    int pid = -1;
    int pidfd = -1;
#endif
};

// Event loop, one per ingest thread. Wakes up only for events or timer deadlines; there is no periodic polling.
//  - Windows: WaitForMultipleObjectsEx (+ RegisterWaitForSingleObject beyond 63 handles)
//  - Linux:   epoll + eventfd + inotify + pidfd
//  - macOS:   kqueue
// All methods except post() and stop() must be called on the loop thread (or before run()).
class EventLoop
{
public:
    using Id = uint64_t;
    using Callback = std::function<void()>;
    using TimePoint = std::chrono::steady_clock::time_point;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // Windows: 'handle' is a waitable object; the callback runs when it is signaled. Manual-reset objects must be reset by the callback.
    // POSIX:   'handle' is a file descriptor; the callback runs while it is readable (level triggered).
    Id watch(NativeHandle handle, Callback callback);
    // one-shot: the callback runs once when the process has exited
    Id watchProcess(ProcessRef process, Callback callback);
    // the callback runs when 'path' (or an entry of its directory) may have changed: written, created, renamed or deleted.
    // Notifications can be delayed or coalesced; callers should combine this with a slow adaptive poll.
    Id watchFile(const std::filesystem::path& path, Callback callback);
    void unwatch(Id id);

    Id addTimer(TimePoint when, Callback callback);
    void cancelTimer(Id id);

    // thread-safe: run 'fn' on the loop thread
    void post(Callback fn);
    // runs until stop() is called
    void run();
    // runs until 'deadline' or stop(); for tests
    void runUntil(TimePoint deadline);
    // thread-safe
    void stop();

    class Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

// Opaque file identity, used to detect log rotation (FILE_ID_INFO on Windows, st_dev/st_ino on POSIX)
struct FileId
{
    uint64_t volume = 0;
    uint64_t low = 0;
    uint64_t high = 0;
    bool operator==(const FileId&) const = default;
};

// A read-only file that does not prevent other processes from writing, renaming or deleting it.
class File
{
public:
    File() = default;
    ~File();
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    static std::optional<File> open(const std::filesystem::path& path, std::string& error);
    static std::optional<FileId> idOf(const std::filesystem::path& path);

    bool isOpen() const;
    void close();
    // returns the number of bytes read, 0 at end of file, -1 on error
    int64_t read(void* buffer, size_t size);
    bool seek(uint64_t offset);
    // the current size of the open file, -1 on error
    int64_t size() const;
    std::optional<FileId> id() const;

private:
#ifdef _WIN32
    void* m_handle = nullptr;
#else
    int m_fd = -1;
#endif
};

// Non-blocking reads from a pipe or stream.
//  - Windows: overlapped I/O on a handle that was opened with FILE_FLAG_OVERLAPPED; waitHandle() is the completion event
//  - POSIX:   a non-blocking fd; waitHandle() is the fd itself
class AsyncReader
{
public:
    // takes ownership of 'handle'
    explicit AsyncReader(NativeHandle handle);
    ~AsyncReader();
    AsyncReader(const AsyncReader&) = delete;
    AsyncReader& operator=(const AsyncReader&) = delete;

    NativeHandle waitHandle() const;
    enum class Status
    {
        Ok,
        Eof,
    };
    // appends all currently available data to 'out' without blocking
    Status read(std::string& out);

    class Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

struct ChildProcessSpec
{
    std::string program;                // UTF-8 path or name (searched in PATH)
    std::vector<std::string> arguments; // UTF-8
    std::string workingDirectory;       // UTF-8, empty = inherit
    std::vector<std::pair<std::string, std::string>> environment; // additions
};

// A child process with captured stdout and stderr; stdin is the null device.
class ChildProcess
{
public:
    static std::unique_ptr<ChildProcess> start(const ChildProcessSpec& spec, std::string& error);
    virtual ~ChildProcess() = default;

    virtual uint32_t pid() const = 0;
    virtual AsyncReader& stdoutReader() = 0;
    virtual AsyncReader& stderrReader() = 0;
    virtual ProcessRef processRef() const = 0;
    // valid after the process exited, e.g. "exit code 1", "exit code 0xC0000005 (access violation)", "killed by signal SIGSEGV"
    virtual std::string exitDescription() = 0;
    virtual void terminate() = 0;
};

// Sockets, implemented with Winsock or BSD sockets.
struct Endpoint
{
    std::string address; // numeric IPv4 or IPv6 address
    uint16_t port = 0;
};

class UdpSocket
{
public:
    // bindAddress: "0.0.0.0", "::" or a local address. multicastGroup: optional group to join.
    static std::unique_ptr<UdpSocket> open(const std::string& bindAddress, uint16_t port, const std::string& multicastGroup, std::string& error);
    virtual ~UdpSocket() = default;
    virtual NativeHandle waitHandle() const = 0;
    // receives one pending datagram without blocking; returns false when none is pending
    virtual bool receive(std::string& data, Endpoint& from) = 0;
};

class TcpStream
{
public:
    virtual ~TcpStream() = default;
    virtual NativeHandle waitHandle() const = 0;
    virtual Endpoint peer() const = 0;
    virtual AsyncReader::Status read(std::string& out) = 0;
    virtual bool write(std::string_view data) = 0; // blocking, for small requests
};

class TcpListener
{
public:
    static std::unique_ptr<TcpListener> open(const std::string& bindAddress, uint16_t port, std::string& error);
    virtual ~TcpListener() = default;
    virtual NativeHandle waitHandle() const = 0;
    // accepts one pending connection without blocking, or returns nullptr
    virtual std::unique_ptr<TcpStream> accept() = 0;
};

// blocking connect with a timeout; call it from a helper thread, not from the event loop
std::unique_ptr<TcpStream> tcpConnect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout, std::string& error);

// sends a UDP datagram (used by 'dvcli -u'); address may be a broadcast address
bool sendUdp(const std::string& address, uint16_t port, std::string_view data, std::string& error);

// Misc
std::string processName(uint32_t pid); // executable file name of a running process, or "" when unknown
uint32_t currentProcessId();
std::filesystem::path configDirectory(); // per-user settings directory for DebugVision
uint64_t privateMemoryUsage();           // bytes
std::string pathToUtf8(const std::filesystem::path& path);
std::filesystem::path utf8ToPath(std::string_view utf8);
std::string lastErrorMessage(); // description of errno / GetLastError()
bool isPipe(int fd);            // stdin/stdout redirection checks
bool isRegularFile(int fd);
bool isConsole(int fd);
void writeConsole(int fd, std::string_view utf8); // stdout/stderr output, UTF-16 console API on Windows

} // namespace dv::platform
