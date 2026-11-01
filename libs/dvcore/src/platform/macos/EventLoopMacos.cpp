#include "dvcore/Platform.h"
#include "../posix/LoopCommon.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <libproc.h>
#include <mach/mach.h>
#include <stdexcept>
#include <sys/event.h>
#include <unistd.h>

namespace dv::platform {

namespace {

constexpr EventLoop::Id wakeId = 0;

void* toUdata(EventLoop::Id id)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(id));
}

EventLoop::Id fromUdata(void* udata)
{
    return static_cast<EventLoop::Id>(reinterpret_cast<uintptr_t>(udata));
}

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
        int fd = -1;    // Fd: the watched descriptor
        int pid = -1;   // Process
        int dirFd = -1; // File: O_EVTONLY descriptors
        int fileFd = -1;
        Callback callback;
    };

    Impl()
    {
        m_kq = kqueue();
        if (m_kq < 0)
            throw std::runtime_error("kqueue creation failed");
        struct kevent ev;
        EV_SET(&ev, 0, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, toUdata(wakeId));
        kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
    }

    ~Impl()
    {
        while (!m_watches.empty())
            unwatch(m_watches.begin()->first);
        ::close(m_kq);
    }

    Id watchFd(int fd, Callback callback)
    {
        auto id = m_nextId++;
        Watch w;
        w.fd = fd;
        w.callback = std::move(callback);
        m_watches.emplace(id, std::move(w));
        struct kevent ev;
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, toUdata(id));
        kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
        return id;
    }

    Id watchProcess(ProcessRef process, Callback callback)
    {
        auto id = m_nextId++;
        Watch w;
        w.kind = Kind::Process;
        w.pid = process.pid;
        w.callback = std::move(callback);
        m_watches.emplace(id, std::move(w));
        struct kevent ev;
        EV_SET(&ev, process.pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, toUdata(id));
        if (kevent(m_kq, &ev, 1, nullptr, 0, nullptr) != 0 && errno == ESRCH)
            m_posted.push([this, id] { fireProcess(id); }); // already exited
        return id;
    }

    Id watchFile(const std::filesystem::path& path, Callback callback)
    {
        auto dir = path.parent_path();
        if (dir.empty())
            dir = ".";
        auto id = m_nextId++;
        Watch w;
        w.kind = Kind::File;
        w.callback = std::move(callback);
        w.dirFd = ::open(dir.c_str(), O_EVTONLY);
        w.fileFd = ::open(path.c_str(), O_EVTONLY);
        if (w.dirFd < 0 && w.fileFd < 0)
            return 0;
        struct kevent ev[2];
        int n = 0;
        if (w.dirFd >= 0)
            EV_SET(&ev[n++], w.dirFd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, toUdata(id));
        if (w.fileFd >= 0)
            EV_SET(&ev[n++], w.fileFd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB, 0, toUdata(id));
        kevent(m_kq, ev, n, nullptr, 0, nullptr);
        m_watches.emplace(id, std::move(w));
        return id;
    }

    void unwatch(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto& w = it->second;
        struct kevent ev;
        switch (w.kind)
        {
        case Kind::Fd:
            EV_SET(&ev, w.fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            break;
        case Kind::Process:
            EV_SET(&ev, w.pid, EVFILT_PROC, EV_DELETE, NOTE_EXIT, 0, nullptr);
            kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
            break;
        case Kind::File:
            // closing the descriptors removes their kevents
            if (w.dirFd >= 0)
                ::close(w.dirFd);
            if (w.fileFd >= 0)
                ::close(w.fileFd);
            break;
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
        struct kevent events[64];
        m_posted.runAll();
        while (!m_stop)
        {
            auto now = std::chrono::steady_clock::now();
            m_timers.runDue(now);
            if (m_stop || (deadline && now >= *deadline))
                break;
            auto timeoutMs = m_timers.timeoutMs(deadline);
            timespec ts{timeoutMs / 1000, static_cast<long>(timeoutMs % 1000) * 1000000};
            auto n = kevent(m_kq, nullptr, 0, events, 64, timeoutMs < 0 ? nullptr : &ts);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("kevent failed");
            }
            std::vector<Id> fired;
            for (int i = 0; i < n; ++i)
            {
                auto id = fromUdata(events[i].udata);
                if (std::find(fired.begin(), fired.end(), id) == fired.end())
                    fired.push_back(id);
            }
            for (auto id : fired)
            {
                if (m_stop)
                    break;
                dispatch(id);
            }
        }
    }

private:
    void wake()
    {
        struct kevent ev;
        EV_SET(&ev, 0, EVFILT_USER, 0, NOTE_TRIGGER, 0, toUdata(wakeId));
        kevent(m_kq, &ev, 1, nullptr, 0, nullptr);
    }

    void fireProcess(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto cb = std::move(it->second.callback);
        m_watches.erase(it); // EV_ONESHOT already removed the kevent
        cb();
    }

    void dispatch(Id id)
    {
        if (id == wakeId)
        {
            m_posted.runAll();
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

    int m_kq = -1;
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

// ---------------------------------------------------------------------------------------------------- macOS specific misc

std::string processName(uint32_t pid)
{
    char name[2 * MAXCOMLEN + 1] = {};
    if (proc_name(static_cast<int>(pid), name, sizeof(name)) <= 0)
        return {};
    return name;
}

std::filesystem::path configDirectory()
{
    if (auto home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / "Library" / "Application Support" / "DebugVision";
    return {};
}

uint64_t privateMemoryUsage()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return info.phys_footprint;
}

} // namespace dv::platform
