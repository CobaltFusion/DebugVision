#include <chrono>

#define FMT_HEADER_ONLY
#include "fmt/format.h"

#include <string_view>

#ifdef  _GNU_SOURCE

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef SYS_gettid

namespace dbgvision {

using tid_t = ::pid_t;
using pid_t = ::pid_t;

pid_t gettid()
{
    return (pid_t)syscall(SYS_gettid);
}

pid_t getpid()
{
    return ::getpid();
}

} // dbgvision

#else
#error "SYS_gettid unavailable on this system"
#endif

#else

#include <windows.h>

namespace dbgvision {

    using tid_t = DWORD;
    using pid_t = DWORD;

    DWORD gettid()
    {
        return ::GetCurrentThreadId();
    }

    DWORD getpid()
    {
        return ::GetCurrentProcessId();
    }

} // dbgvision


#endif //  _GNU_SOURCE

namespace dbgvision {

    std::string get_current_process_name()
    {
#if defined(__APPLE__) || defined(__FreeBSD__)
        return getprogname();
#elif defined(_GNU_SOURCE)
        return program_invocation_name;
#elif defined(_WIN32)
        return __argv[0];
#else
        return "?";
#endif
    }

} // namespace dbgvision

namespace dbgvision {

    double LogTimepoint(std::string_view id)
    {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now);
        double elapsedMilliseconds = elapsed.count() / 1000.0;
        static auto pid = dbgvision::getpid();
        thread_local auto tid = dbgvision::gettid();
        fmt::print("sta;{};{};{};{};{};end;\n", pid, dbgvision::get_current_process_name(), tid, id, elapsedMilliseconds);
        return elapsedMilliseconds;
    }

} // namespace dbgvision
