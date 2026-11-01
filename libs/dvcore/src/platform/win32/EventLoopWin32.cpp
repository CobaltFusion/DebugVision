#include "dvcore/Platform.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace dv::platform {

namespace {

constexpr size_t maxDirectHandles = MAXIMUM_WAIT_OBJECTS - 1; // one slot is the wake event

std::wstring directoryOf(const std::filesystem::path& path)
{
    auto dir = path.parent_path();
    if (dir.empty())
        dir = std::filesystem::current_path();
    return dir.wstring();
}

} // namespace

class EventLoop::Impl
{
public:
    enum class Kind
    {
        Handle,
        FileChange,
        Process,
    };

    struct RegisteredWait
    {
        Impl* impl;
        Id id;
    };

    struct Watch
    {
        HANDLE handle = nullptr;
        Callback callback;
        Kind kind = Kind::Handle;
        bool direct = false; // part of the WaitForMultipleObjects array
        HANDLE registeredWait = nullptr;
        std::unique_ptr<RegisteredWait> context;
    };

    Impl()
    {
        m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_wakeEvent)
            throw std::runtime_error("CreateEvent failed");
    }

    ~Impl()
    {
        while (!m_watches.empty())
            unwatch(m_watches.begin()->first);
        CloseHandle(m_wakeEvent);
    }

    Id watch(HANDLE handle, Callback callback, Kind kind)
    {
        auto id = m_nextId++;
        Watch w;
        w.handle = handle;
        w.callback = std::move(callback);
        w.kind = kind;
        auto& watch = m_watches.emplace(id, std::move(w)).first->second;
        if (kind != Kind::Process && m_directCount < maxDirectHandles)
        {
            watch.direct = true;
            ++m_directCount;
            m_dirty = true;
        }
        else
        {
            registerWait(id, watch);
        }
        return id;
    }

    void unwatch(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto& w = it->second;
        if (w.registeredWait)
            UnregisterWaitEx(w.registeredWait, INVALID_HANDLE_VALUE); // waits for a running callback
        if (w.direct)
        {
            --m_directCount;
            m_dirty = true;
        }
        if (w.kind == Kind::FileChange)
            FindCloseChangeNotification(w.handle);
        m_watches.erase(it);
    }

    Id addTimer(TimePoint when, Callback callback)
    {
        auto id = m_nextId++;
        auto pos = m_timerQueue.emplace(when, id);
        m_timers.emplace(id, std::make_pair(pos, std::move(callback)));
        return id;
    }

    void cancelTimer(Id id)
    {
        auto it = m_timers.find(id);
        if (it == m_timers.end())
            return;
        m_timerQueue.erase(it->second.first);
        m_timers.erase(it);
    }

    void post(Callback fn)
    {
        {
            std::lock_guard lock(m_postMutex);
            m_posted.push_back(std::move(fn));
        }
        SetEvent(m_wakeEvent);
    }

    void stop()
    {
        m_stop = true;
        SetEvent(m_wakeEvent);
    }

    void run(std::optional<TimePoint> deadline)
    {
        while (!m_stop)
        {
            auto now = std::chrono::steady_clock::now();
            runTimers(now);
            if (m_stop)
                break;
            if (deadline && now >= *deadline)
                break;

            DWORD timeout = INFINITE;
            auto next = nextDeadline();
            if (deadline && (!next || *deadline < *next))
                next = deadline;
            if (next)
            {
                auto ms = std::chrono::ceil<std::chrono::milliseconds>(*next - std::chrono::steady_clock::now()).count();
                timeout = static_cast<DWORD>(std::clamp<long long>(ms, 0, 0x7fffffff));
            }

            if (m_dirty)
                rebuild();
            auto count = static_cast<DWORD>(m_handles.size());
            auto result = WaitForMultipleObjectsEx(count, m_handles.data(), FALSE, timeout, FALSE);
            if (result == WAIT_TIMEOUT)
                continue;
            if (result == WAIT_FAILED)
                throw std::runtime_error("WaitForMultipleObjects failed");
            auto index = result - WAIT_OBJECT_0;
            if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count)
                index = result - WAIT_ABANDONED_0;
            if (index >= count)
                continue;

            // copy: callbacks may add or remove watches
            auto ids = m_ids;
            auto handles = m_handles;
            dispatch(ids[index]);
            // fairness: WaitForMultipleObjects always reports the lowest signaled index, so service the others too
            for (auto j = index + 1; j < count && !m_stop; ++j)
            {
                if (WaitForSingleObject(handles[j], 0) == WAIT_OBJECT_0)
                    dispatch(ids[j]);
            }
        }
    }

private:
    static void CALLBACK onRegisteredWait(void* context, BOOLEAN /*timedOut*/)
    {
        auto ctx = static_cast<RegisteredWait*>(context);
        auto impl = ctx->impl;
        auto id = ctx->id;
        impl->post([impl, id] { impl->onRegisteredWaitFired(id); });
    }

    void registerWait(Id id, Watch& watch)
    {
        watch.context = std::make_unique<RegisteredWait>(RegisteredWait{this, id});
        if (!RegisterWaitForSingleObject(&watch.registeredWait, watch.handle, &Impl::onRegisteredWait, watch.context.get(), INFINITE, WT_EXECUTEONLYONCE))
            watch.registeredWait = nullptr;
    }

    void onRegisteredWaitFired(Id id)
    {
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        auto& w = it->second;
        UnregisterWaitEx(w.registeredWait, INVALID_HANDLE_VALUE);
        w.registeredWait = nullptr;
        if (w.kind == Kind::Process)
        {
            auto cb = std::move(w.callback);
            m_watches.erase(it);
            cb();
            return;
        }
        if (w.kind == Kind::FileChange)
            FindNextChangeNotification(w.handle);
        auto cb = w.callback;
        cb();
        // re-arm, unless the callback removed the watch
        it = m_watches.find(id);
        if (it != m_watches.end())
            registerWait(id, it->second);
    }

    void dispatch(Id id)
    {
        if (id == 0)
        {
            runPosted();
            return;
        }
        auto it = m_watches.find(id);
        if (it == m_watches.end())
            return;
        if (it->second.kind == Kind::FileChange)
            FindNextChangeNotification(it->second.handle);
        auto cb = it->second.callback; // copy, the callback may unwatch itself
        cb();
    }

    void runPosted()
    {
        std::vector<Callback> posted;
        {
            std::lock_guard lock(m_postMutex);
            posted.swap(m_posted);
        }
        for (auto& fn : posted)
            fn();
    }

    void runTimers(TimePoint now)
    {
        while (!m_timerQueue.empty() && m_timerQueue.begin()->first <= now)
        {
            auto id = m_timerQueue.begin()->second;
            m_timerQueue.erase(m_timerQueue.begin());
            auto it = m_timers.find(id);
            auto cb = std::move(it->second.second);
            m_timers.erase(it);
            cb();
        }
    }

    std::optional<TimePoint> nextDeadline() const
    {
        if (m_timerQueue.empty())
            return std::nullopt;
        return m_timerQueue.begin()->first;
    }

    void rebuild()
    {
        m_handles.clear();
        m_ids.clear();
        m_handles.push_back(m_wakeEvent);
        m_ids.push_back(0);
        for (auto& [id, w] : m_watches)
        {
            if (!w.direct)
                continue;
            m_handles.push_back(w.handle);
            m_ids.push_back(id);
        }
        m_dirty = false;
    }

    HANDLE m_wakeEvent = nullptr;
    std::map<Id, Watch> m_watches;
    size_t m_directCount = 0;
    bool m_dirty = true;
    std::vector<HANDLE> m_handles;
    std::vector<Id> m_ids;
    Id m_nextId = 1;

    std::multimap<TimePoint, Id> m_timerQueue;
    std::unordered_map<Id, std::pair<std::multimap<TimePoint, Id>::iterator, Callback>> m_timers;

    std::mutex m_postMutex;
    std::vector<Callback> m_posted;
    std::atomic<bool> m_stop{false};
};

EventLoop::EventLoop() :
    m_impl(std::make_unique<Impl>())
{
}

EventLoop::~EventLoop() = default;

EventLoop::Id EventLoop::watch(NativeHandle handle, Callback callback)
{
    return m_impl->watch(static_cast<HANDLE>(handle), std::move(callback), Impl::Kind::Handle);
}

EventLoop::Id EventLoop::watchProcess(ProcessRef process, Callback callback)
{
    return m_impl->watch(static_cast<HANDLE>(process.handle), std::move(callback), Impl::Kind::Process);
}

EventLoop::Id EventLoop::watchFile(const std::filesystem::path& path, Callback callback)
{
    auto handle = FindFirstChangeNotificationW(directoryOf(path).c_str(), FALSE, FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    return m_impl->watch(handle, std::move(callback), Impl::Kind::FileChange);
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

} // namespace dv::platform
