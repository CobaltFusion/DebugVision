#pragma once

// Timer and cross-thread post queue shared by the Linux and macOS event loops

#include "dvcore/Platform.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace dv::platform {

class TimerQueue
{
public:
    using Id = EventLoop::Id;
    using TimePoint = EventLoop::TimePoint;

    void add(Id id, TimePoint when, EventLoop::Callback callback)
    {
        auto pos = m_queue.emplace(when, id);
        m_timers.emplace(id, std::make_pair(pos, std::move(callback)));
    }

    void cancel(Id id)
    {
        auto it = m_timers.find(id);
        if (it == m_timers.end())
            return;
        m_queue.erase(it->second.first);
        m_timers.erase(it);
    }

    void runDue(TimePoint now)
    {
        while (!m_queue.empty() && m_queue.begin()->first <= now)
        {
            auto id = m_queue.begin()->second;
            m_queue.erase(m_queue.begin());
            auto it = m_timers.find(id);
            auto cb = std::move(it->second.second);
            m_timers.erase(it);
            cb();
        }
    }

    // milliseconds until the next timer (or 'deadline'), -1 = infinite
    int timeoutMs(std::optional<TimePoint> deadline) const
    {
        std::optional<TimePoint> next;
        if (!m_queue.empty())
            next = m_queue.begin()->first;
        if (deadline && (!next || *deadline < *next))
            next = deadline;
        if (!next)
            return -1;
        auto ms = std::chrono::ceil<std::chrono::milliseconds>(*next - std::chrono::steady_clock::now()).count();
        return static_cast<int>(std::clamp<long long>(ms, 0, 0x7fffffff));
    }

private:
    std::multimap<TimePoint, Id> m_queue;
    std::unordered_map<Id, std::pair<std::multimap<TimePoint, Id>::iterator, EventLoop::Callback>> m_timers;
};

class PostQueue
{
public:
    void push(EventLoop::Callback fn)
    {
        std::lock_guard lock(m_mutex);
        m_posted.push_back(std::move(fn));
    }

    void runAll()
    {
        std::vector<EventLoop::Callback> posted;
        {
            std::lock_guard lock(m_mutex);
            posted.swap(m_posted);
        }
        for (auto& fn : posted)
            fn();
    }

private:
    std::mutex m_mutex;
    std::vector<EventLoop::Callback> m_posted;
};

} // namespace dv::platform
