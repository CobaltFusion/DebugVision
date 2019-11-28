#pragma once

#include "Infra/cdbg.h"
#include "Infra/stringbuilder.h"
#include <chrono>
#include <thread>

namespace infra {
namespace logfunction {

class Entry
{
public:
    using clock = std::chrono::steady_clock;

    Entry(const Entry&) = delete;

    Entry(const std::string f) :
        m_function(f),
        m_start(clock::now())
    {
        m_builder << m_function << "(";
        Parse();
    }

    template <typename T, typename... Targs>
    Entry(const std::string f, T value, Targs... args) :
        m_function(f),
        m_start(clock::now())
    {
        m_builder << m_function << "(" << value;
        Parse(args...);
    }

    template <typename T, typename... Targs>
    void Parse(T value, Targs... args)
    {
        m_builder << ", " << value;
        Parse(args...);
    }

    void Parse()
    {
        cdbg() << m_builder.str() << ") enter (tid: " << std::this_thread::get_id() << ")\n";
    }

    ~Entry()
    {
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - m_start);
        if (std::uncaught_exceptions() == 0)
            cdbg() << m_function << "() leave (tid: " << std::this_thread::get_id() << ") " << dt.count() << "ms\n";
        else
            cdbg() << m_function << "() exception (tid: " << std::this_thread::get_id() << ") " << dt.count() << "ms\n";
    }

private:
    std::string m_function;
    infra::stringbuilder m_builder;
    clock::time_point m_start;
};

// thread_local unsigned int indent = 0; // increase by 4 every entry, decrease at every leave

class EntryReturn
{
public:
    using clock = std::chrono::steady_clock;

    EntryReturn(const Entry&) = delete;

    template <typename T, typename... Targs>
    EntryReturn(const std::string f, T value, Targs... args) :
        m_function(f),
        m_start(clock::now())
    {
        m_builder << m_function << "(" << value;
        Parse(args...);
    }

    template <typename T, typename... Targs>
    void Parse(T value, Targs... args)
    {
        m_builder << "," << value;
        Parse(args...);
    }

    template <typename T>
    void Parse(T* value)
    {
        m_retVarToString = [value]() -> std::string { return stringbuilder() << *value; };
        cdbg() << m_builder.str() << ") enter (tid: " << std::this_thread::get_id() << ")\n";
    }

    ~EntryReturn()
    {
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - m_start);
        if (std::uncaught_exceptions() == 0)
            cdbg() << m_function << "() leave -> " << m_retVarToString() << " (tid: " << std::this_thread::get_id() << ") " << dt.count() << "ms\n";
        else
            cdbg() << m_function << "() exception (tid: " << std::this_thread::get_id() << ") " << dt.count() << "ms\n";
    }

private:
    std::string m_function;
    infra::stringbuilder m_builder;
    clock::time_point m_start;
    std::function<std::string()> m_retVarToString;
};

} // namespace logfunction

class Timer
{
    using clock = std::chrono::steady_clock;

public:
    Timer() :
        m_startTime(clock::now())
    {
    }

    void Reset()
    {
        m_startTime = clock::now();
    }

    clock::duration Delta()
    {
        return clock::now() - m_startTime;
    }

    template <class T>
    auto Delta()
    {
        std::chrono::duration<double, T> d = Delta();
        return d.count();
    }

    std::string DeltaAsString(const std::string& id)
    {
        std::stringstream ss;
        ss << "[" << id << "] " << Delta<std::milli>() << " ms";
        return ss.str();
    }

private:
    clock::time_point m_startTime;
};


} // namespace infra


#define LOG_FUNCTION(...) infra::logfunction::Entry log_function_can_only_be_used_once_(__FUNCTION__, ##__VA_ARGS__);
#define LOG_FUNCTION_RETURN(...) infra::logfunction::EntryReturn log_function_can_only_be_used_once_(__FUNCTION__, ##__VA_ARGS__);
