#include "dvcore/Platform.h"
#include "../posix/LoopCommon.h"

#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dv::platform {

namespace {

constexpr EventLoop::Id wakeId = 0;
constexpr EventLoop::Id inotifyId = UINT64_MAX;
constexpr auto processPollInterval = std::chrono::milliseconds(250); // only for kernels without pidfd (< 5.3)

} // namespace

class EventLoop::Impl
{
public:
    enum class Kind
    {
        Fd,
        Process,
        File,
    };

    struct Watch
    {
        Kind kind = Kind::Fd;
        int fd = -1;
        Callback callback;
        int wd = -1;      // inotify watch descriptor
        std::string name; // file name within the watched directory
        int pid = -1;
        Id pollTimer = 0;
    };

    Impl()
    {
        m_epoll = epoll_create1(EPOLL_CLOEXEC);
        m_wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (m_epoll < 0 || m_wake < 0)
            throw std::runtime_error("epoll/eventfd creation failed");
        add(m_wake, wakeId);
    }

    ~Impl()
    {
        while (!m_watches.empty())
            unwatch(m_watches.begin()->first);
        if (m_inotify >= 0)
            ::close(m_inotify);
        ::close(m_wake);
        ::close(m_epoll);
    }

    Id watchFd(int fd, Callback callback)
    {
        auto id = m_nextId++;
        Watch w;
        w.fd = fd;
        w.callback = std::move(callback);
        m_watches.emplace(id, std::move(w));
        add(fd, id);
        return id;
    }

    Id watchProcess(ProcessRef process, Callback callback)
    {
        auto id = m_nextId++;
        Watch w;
        w.kind = Kind::Process;
        w.fd = process.pidfd;
        w.pid = process.pid;
        w.callback = std::move(callback);
        auto& watch = m_watches.emplace(id, std::move(w)).first->second;
        if (watch.fd >= 0)
            add(watch.fd, id);
        else
            schedulePoll(id, watch);
        return id;
    }

    Id watchFile(const std::filesystem::path& path, Callback callback)
    {
        if (m_inotify < 0)
        {
            m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            if (m_inotify < 0)
                return 0;
            add(m_inotify, inotifyId);
        }
        auto dir = path.parent_path();
        if (dir.empty())
            dir = ".";
        auto wd = inotify_add_watch(m_inotify, dir.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB);
        if (wd < 0)
            return 0;
        auto id = m_nextId++;
        Watch w;
        w.kind = Kind::File;
        w.wd = wd;
        w.name = path.filename().string();
        w.callback = std::move(callback);
        m_watches.emplace(id, std::move(w));
        return id;
    }

    void unwatch(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto& w = it->second;
        switch (w.kind)
        {
        case Kind::Fd:
            epoll_ctl(m_epoll, EPOLL_CTL_DEL, w.fd, nullptr);
            break;
        case Kind::Process:
            if (w.fd >= 0)
                epoll_ctl(m_epoll, EPOLL_CTL_DEL, w.fd, nullptr);
            if (w.pollTimer)
                m_timers.cancel(w.pollTimer);
            break;
        case Kind::File:
        {
            auto wd = w.wd;
            m_watches.erase(it);
            bool shared = false;
            for (auto& [otherId, other] : m_watches)
                shared = shared || (other.kind == Kind::File && other.wd == wd);
            if (!shared)
                inotify_rm_watch(m_inotify, wd);
            return;
        }
        }
        m_watches.erase(it);
    }

    Id addTimer(TimePoint when, Callback callback)
    {
        auto id = m_nextId++;
        m_timers.add(id, when, std::move(callback));
        return id;
    }

    void cancelTimer(Id id) { m_timers.cancel(id); }

    void post(Callback fn)
    {
        m_posted.push(std::move(fn));
        wake();
    }

    void stop()
    {
        m_stop = true;
        wake();
    }

    void run(std::optional<TimePoint> deadline)
    {
        epoll_event events[64];
        while (!m_stop)
        {
            auto now = std::chrono::steady_clock::now();
            m_timers.runDue(now);
            if (m_stop || (deadline && now >= *deadline))
                break;
            auto n = epoll_wait(m_epoll, events, 64, m_timers.timeoutMs(deadline));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("epoll_wait failed");
            }
            for (int i = 0; i < n && !m_stop; ++i)
                dispatch(events[i].data.u64);
        }
    }

private:
    void add(int fd, Id id)
    {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.u64 = id;
        epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &ev);
    }

    void wake()
    {
        uint64_t one = 1;
        [[maybe_unused]] auto n = ::write(m_wake, &one, sizeof(one));
    }

    void schedulePoll(Id id, Watch& watch)
    {
        watch.pollTimer = addTimer(std::chrono::steady_clock::now() + processPollInterval, [this, id] {
            auto it = m_watches.find(id);
            if (it == m_watches.end())
                return;
            it->second.pollTimer = 0;
            siginfo_t info{};
            if (waitid(P_PID, static_cast<id_t>(it->second.pid), &info, WEXITED | WNOHANG | WNOWAIT) == 0 && info.si_pid == it->second.pid)
                fireProcess(id);
            else
                schedulePoll(id, it->second);
        });
    }

    void fireProcess(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto cb = std::move(it->second.callback);
        unwatch(id);
        cb();
    }

    void dispatch(Id id)
    {
        if (id == wakeId)
        {
            uint64_t value;
            [[maybe_unused]] auto n = ::read(m_wake, &value, sizeof(value));
            m_posted.runAll();
            return;
        }
        if (id == inotifyId)
        {
            dispatchInotify();
            return;
        }
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        if (it->second.kind == Kind::Process)
        {
            fireProcess(id);
            return;
        }
        auto cb = it->second.callback;
        cb();
    }

    void dispatchInotify()
    {
        alignas(inotify_event) char buffer[16 * 1024];
        std::vector<Id> toNotify;
        for (;;)
        {
            auto n = ::read(m_inotify, buffer, sizeof(buffer));
            if (n <= 0)
                break;
            for (char* p = buffer; p < buffer + n;)
            {
                auto ev = reinterpret_cast<inotify_event*>(p);
                std::string_view name = ev->len > 0 ? std::string_view(ev->name) : std::string_view();
                for (auto& [wid, w] : m_watches)
                {
                    if (w.kind == Kind::File && w.wd == ev->wd && (name.empty() || w.name.empty() || name == w.name))
                    {
                        if (std::find(toNotify.begin(), toNotify.end(), wid) == toNotify.end())
                            toNotify.push_back(wid);
                    }
                }
                p += sizeof(inotify_event) + ev->len;
            }
        }
        for (auto wid : toNotify)
        {
            auto it = m_watches.find(wid);
            if (it == m_watches.end())
                continue;
            auto cb = it->second.callback;
            cb();
        }
    }

    int m_epoll = -1;
    int m_wake = -1;
    int m_inotify = -1;
    std::map<Id, Watch> m_watches;
    Id m_nextId = 1;
    TimerQueue m_timers;
    PostQueue m_posted;
    std::atomic<bool> m_stop{false};
};

EventLoop::EventLoop() :
    m_impl(std::make_unique<Impl>())
{
}

EventLoop::~EventLoop() = default;

EventLoop::Id EventLoop::watch(NativeHandle handle, Callback callback)
{
    return m_impl->watchFd(handle, std::move(callback));
}

EventLoop::Id EventLoop::watchProcess(ProcessRef process, Callback callback)
{
    return m_impl->watchProcess(process, std::move(callback));
}

EventLoop::Id EventLoop::watchFile(const std::filesystem::path& path, Callback callback)
{
    return m_impl->watchFile(path, std::move(callback));
}

void EventLoop::unwatch(Id id)
{
    m_impl->unwatch(id);
}

EventLoop::Id EventLoop::addTimer(TimePoint when, Callback callback)
{
    return m_impl->addTimer(when, std::move(callback));
}

void EventLoop::cancelTimer(Id id)
{
    m_impl->cancelTimer(id);
}

void EventLoop::post(Callback fn)
{
    m_impl->post(std::move(fn));
}

void EventLoop::run()
{
    m_impl->run(std::nullopt);
}

void EventLoop::runUntil(TimePoint deadline)
{
    m_impl->run(deadline);
}

void EventLoop::stop()
{
    m_impl->stop();
}

// ---------------------------------------------------------------------------------------------------- Linux specific misc

std::string processName(uint32_t pid)
{
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(comm, name);
    return name;
}

std::filesystem::path configDirectory()
{
    if (auto xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "debugvision";
    if (auto home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "debugvision";
    return {};
}

uint64_t privateMemoryUsage()
{
    // resident set size minus shared pages, from /proc/self/statm (in pages)
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0;
    uint64_t resident = 0;
    uint64_t shared = 0;
    statm >> size >> resident >> shared;
    return (resident - std::min(resident, shared)) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
}

} // namespace dv::platform
