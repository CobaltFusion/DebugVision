// Shared POSIX implementation (Linux and macOS): files, pipes, child processes and misc helpers

#include "dvcore/Platform.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

extern char** environ;

namespace dv::platform {

std::string lastErrorMessage()
{
    return std::strerror(errno);
}

// ---------------------------------------------------------------------------------------------------- File

File::~File()
{
    close();
}

File::File(File&& other) noexcept :
    m_fd(other.m_fd)
{
    other.m_fd = -1;
}

File& File::operator=(File&& other) noexcept
{
    if (this != &other)
    {
        close();
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

std::optional<File> File::open(const std::filesystem::path& path, std::string& error)
{
    auto fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        error = lastErrorMessage();
        return std::nullopt;
    }
    File file;
    file.m_fd = fd;
    return file;
}

std::optional<FileId> File::idOf(const std::filesystem::path& path)
{
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return std::nullopt;
    return FileId{static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino), 0};
}

bool File::isOpen() const
{
    return m_fd >= 0;
}

void File::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

int64_t File::read(void* buffer, size_t size)
{
    for (;;)
    {
        auto n = ::read(m_fd, buffer, size);
        if (n < 0 && errno == EINTR)
            continue;
        return n;
    }
}

bool File::seek(uint64_t offset)
{
    return ::lseek(m_fd, static_cast<off_t>(offset), SEEK_SET) >= 0;
}

int64_t File::size() const
{
    struct stat st{};
    if (::fstat(m_fd, &st) != 0)
        return -1;
    return st.st_size;
}

std::optional<FileId> File::id() const
{
    struct stat st{};
    if (::fstat(m_fd, &st) != 0)
        return std::nullopt;
    return FileId{static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino), 0};
}

// ---------------------------------------------------------------------------------------------------- AsyncReader

class AsyncReader::Impl
{
public:
    explicit Impl(int fd) :
        m_fd(fd)
    {
        auto flags = fcntl(m_fd, F_GETFL, 0);
        fcntl(m_fd, F_SETFL, flags | O_NONBLOCK);
    }

    ~Impl() { ::close(m_fd); }

    int fd() const { return m_fd; }

    Status read(std::string& out)
    {
        char buffer[64 * 1024];
        for (;;)
        {
            auto n = ::read(m_fd, buffer, sizeof(buffer));
            if (n > 0)
            {
                out.append(buffer, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
                return Status::Eof;
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return Status::Ok;
            return Status::Eof;
        }
    }

private:
    int m_fd;
};

AsyncReader::AsyncReader(NativeHandle handle) :
    m_impl(std::make_unique<Impl>(handle))
{
}

AsyncReader::~AsyncReader() = default;

NativeHandle AsyncReader::waitHandle() const
{
    return m_impl->fd();
}

AsyncReader::Status AsyncReader::read(std::string& out)
{
    return m_impl->read(out);
}

// ---------------------------------------------------------------------------------------------------- ChildProcess

namespace {

std::string signalName(int sig)
{
    switch (sig)
    {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGKILL: return "SIGKILL";
    case SIGTERM: return "SIGTERM";
    case SIGINT: return "SIGINT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGPIPE: return "SIGPIPE";
    case SIGHUP: return "SIGHUP";
    default: return "signal " + std::to_string(sig);
    }
}

class ChildProcessPosix : public ChildProcess
{
public:
    ChildProcessPosix(pid_t pid, int outFd, int errFd) :
        m_pid(pid),
        m_stdout(outFd),
        m_stderr(errFd)
    {
#ifdef __linux__
        m_pidfd = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
#endif
    }

    ~ChildProcessPosix() override
    {
        if (!m_reaped)
        {
            ::kill(m_pid, SIGTERM);
            int status = 0;
            ::waitpid(m_pid, &status, 0);
        }
        if (m_pidfd >= 0)
            ::close(m_pidfd);
    }

    uint32_t pid() const override { return static_cast<uint32_t>(m_pid); }
    AsyncReader& stdoutReader() override { return m_stdout; }
    AsyncReader& stderrReader() override { return m_stderr; }
    ProcessRef processRef() const override { return ProcessRef{m_pid, m_pidfd}; }

    std::string exitDescription() override
    {
        if (!m_reaped)
        {
            if (::waitpid(m_pid, &m_status, WNOHANG) != m_pid)
                return "still running";
            m_reaped = true;
        }
        if (WIFEXITED(m_status))
            return "exit code " + std::to_string(WEXITSTATUS(m_status));
        if (WIFSIGNALED(m_status))
            return "killed by signal " + signalName(WTERMSIG(m_status));
        return "unknown exit status";
    }

    void terminate() override
    {
        if (!m_reaped)
            ::kill(m_pid, SIGTERM);
    }

private:
    pid_t m_pid;
    int m_pidfd = -1;
    AsyncReader m_stdout;
    AsyncReader m_stderr;
    bool m_reaped = false;
    int m_status = 0;
};

bool makePipe(int fds[2])
{
#ifdef __linux__
    return ::pipe2(fds, O_CLOEXEC) == 0;
#else
    if (::pipe(fds) != 0)
        return false;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

} // namespace

std::unique_ptr<ChildProcess> ChildProcess::start(const ChildProcessSpec& spec, std::string& error)
{
    int out[2];
    int err[2];
    if (!makePipe(out))
    {
        error = lastErrorMessage();
        return nullptr;
    }
    if (!makePipe(err))
    {
        error = lastErrorMessage();
        ::close(out[0]);
        ::close(out[1]);
        return nullptr;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_adddup2(&actions, err[1], 2);

    std::vector<std::string> argStorage;
    argStorage.push_back(spec.program);
    argStorage.insert(argStorage.end(), spec.arguments.begin(), spec.arguments.end());
    std::vector<char*> argv;
    for (auto& a : argStorage)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    std::vector<std::string> envStorage;
    for (auto e = environ; e && *e; ++e)
        envStorage.emplace_back(*e);
    for (auto& [key, value] : spec.environment)
    {
        auto prefix = key + "=";
        std::erase_if(envStorage, [&](const std::string& s) { return s.compare(0, prefix.size(), prefix) == 0; });
        envStorage.push_back(prefix + value);
    }
    std::vector<char*> envp;
    for (auto& e : envStorage)
        envp.push_back(e.data());
    envp.push_back(nullptr);

    // posix_spawn has no portable 'chdir' action; temporarily change the directory of this (ingest) thread's process
    std::filesystem::path previousDir;
    if (!spec.workingDirectory.empty())
    {
        std::error_code ec;
        previousDir = std::filesystem::current_path(ec);
        std::filesystem::current_path(spec.workingDirectory, ec);
    }

    pid_t pid = 0;
    auto rc = posix_spawnp(&pid, spec.program.c_str(), &actions, nullptr, argv.data(), envp.data());

    if (!previousDir.empty())
    {
        std::error_code ec;
        std::filesystem::current_path(previousDir, ec);
    }
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    ::close(err[1]);
    if (rc != 0)
    {
        error = std::strerror(rc);
        ::close(out[0]);
        ::close(err[0]);
        return nullptr;
    }
    return std::make_unique<ChildProcessPosix>(pid, out[0], err[0]);
}

// ---------------------------------------------------------------------------------------------------- misc

uint32_t currentProcessId()
{
    return static_cast<uint32_t>(::getpid());
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    return path.native();
}

std::filesystem::path utf8ToPath(std::string_view utf8)
{
    return std::filesystem::path(std::string(utf8));
}

bool isPipe(int fd)
{
    struct stat st{};
    return ::fstat(fd, &st) == 0 && (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode));
}

bool isRegularFile(int fd)
{
    struct stat st{};
    return ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
}

bool isConsole(int fd)
{
    return ::isatty(fd) != 0;
}

void writeConsole(int fd, std::string_view utf8)
{
    while (!utf8.empty())
    {
        auto n = ::write(fd, utf8.data(), utf8.size());
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return;
        }
        utf8.remove_prefix(static_cast<size_t>(n));
    }
}

} // namespace dv::platform
