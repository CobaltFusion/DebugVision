// Child process output (stdout + stderr) and standard input

#include "Sources.h"

#include "dvcore/Clock.h"

#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace dv {

std::string expandProcessName(const std::string& pattern, const std::string& ip, uint16_t port)
{
    std::string result;
    result.reserve(pattern.size() + 16);
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern.compare(i, 4, "{ip}") == 0)
        {
            result += ip;
            i += 3;
        }
        else if (pattern.compare(i, 6, "{port}") == 0)
        {
            result += std::to_string(port);
            i += 5;
        }
        else
        {
            result += pattern[i];
        }
    }
    return result;
}

std::shared_ptr<LegacyCodec> legacyCodecFor(const SourceConfig& config, bool consoleOutput)
{
    auto charset = config.legacyCharset;
    if (charset.empty() && consoleOutput)
        charset = "oem";
    return LegacyCodec::create(charset);
}

namespace {

using namespace std::chrono_literals;

// One captured output stream of a child process. The encoding is detected on the first chunk:
// UTF-16 output (e.g. 'cmd /U') is decoded as UTF-16LE, anything else per line as UTF-8 or the OEM code page.
class StreamCapture
{
public:
    StreamCapture(platform::AsyncReader& reader, Encoding encoding, std::shared_ptr<LegacyCodec> legacy, uint32_t pid, uint32_t origin) :
        m_reader(reader),
        m_decoder(encoding, std::move(legacy)),
        m_pid(pid),
        m_origin(origin)
    {
    }

    // returns false at end of stream
    bool read(Sink& sink, bool paused)
    {
        m_buffer.clear();
        auto status = m_reader.read(m_buffer);
        if (!paused)
            m_decoder.feed(m_buffer, [&](std::string_view line) { sink.add(m_pid, m_origin, line); });
        return status == platform::AsyncReader::Status::Ok;
    }

    void flush(Sink& sink)
    {
        m_decoder.flush([&](std::string_view line) { sink.add(m_pid, m_origin, line); });
    }

private:
    platform::AsyncReader& m_reader;
    LineDecoder m_decoder;
    uint32_t m_pid;
    uint32_t m_origin;
    std::string m_buffer;
};

class ProcessSource : public Source
{
public:
    explicit ProcessSource(const SourceConfig& config) :
        m_config(config),
        m_params(config.getParams<ProcessParams>()),
        m_legacy(legacyCodecFor(config, true))
    {
        m_name = config.processNameOverride.empty() ? platform::pathToUtf8(platform::utf8ToPath(m_params.command).filename()) : config.processNameOverride;
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_stopped = false;
        launch();
    }

    void stop(platform::EventLoop& loop) override
    {
        m_stopped = true;
        if (m_timer)
            loop.cancelTimer(m_timer);
        m_timer = 0;
        if (m_process)
        {
            m_process->terminate();
            cleanup();
        }
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

private:
    void launch()
    {
        platform::ChildProcessSpec spec;
        spec.program = m_params.command;
        spec.arguments = m_params.arguments;
        spec.workingDirectory = m_params.workingDirectory;
        spec.environment = m_params.environment;
        std::string error;
        m_process = platform::ChildProcess::start(spec, error);
        if (!m_process)
        {
            setStatus(SourceState::Error, "error: " + error);
            m_sink->internal("Unable to start '" + m_params.command + "': " + error);
            scheduleRestart();
            return;
        }
        m_startTime = nowSystemNs();
        auto pid = m_process->pid();
        auto encoding = m_config.encodingValue();
        m_stdout = std::make_unique<StreamCapture>(m_process->stdoutReader(), encoding, m_legacy, pid, m_sink->origin(m_name + ":stdout"));
        m_stderr = std::make_unique<StreamCapture>(m_process->stderrReader(), encoding, m_legacy, pid, m_sink->origin(m_name + ":stderr"));
        m_stdoutOpen = true;
        m_stderrOpen = true;
        m_stdoutWatch = m_loop->watch(m_process->stdoutReader().waitHandle(), [this] { onOutput(true); });
        m_stderrWatch = m_loop->watch(m_process->stderrReader().waitHandle(), [this] { onOutput(false); });
        m_exitWatch = m_loop->watchProcess(m_process->processRef(), [this] {
            m_exitWatch = 0;
            m_exited = true;
            maybeFinish();
        });
        m_exited = false;
        setStatus(SourceState::Running, "running " + m_name + " (pid " + std::to_string(pid) + ")");
        m_sink->internal("Started capturing output of " + m_name + " (pid " + std::to_string(pid) + ")");
    }

    void onOutput(bool isStdout)
    {
        auto& capture = isStdout ? m_stdout : m_stderr;
        auto& open = isStdout ? m_stdoutOpen : m_stderrOpen;
        auto& watch = isStdout ? m_stdoutWatch : m_stderrWatch;
        if (!capture || !open)
            return;
        if (!capture->read(*m_sink, m_paused))
        {
            open = false;
            m_loop->unwatch(watch);
            watch = 0;
            maybeFinish();
        }
    }

    // the process is done when it exited and both pipes are drained
    void maybeFinish()
    {
        if (!m_exited)
            return;
        if (m_stdoutOpen || m_stderrOpen)
        {
            // the pipes can stay open when the child started grandchildren that inherited them; drain what is there now
            if (m_stdoutOpen)
                m_stdout->read(*m_sink, m_paused);
            if (m_stderrOpen)
                m_stderr->read(*m_sink, m_paused);
        }
        m_stdout->flush(*m_sink);
        m_stderr->flush(*m_sink);
        auto description = m_process->exitDescription();
        m_sink->internal("<process started at " + formatClockTime(m_startTime) + " has terminated with " + description + ">");
        setStatus(SourceState::Stopped, m_name + " terminated with " + description);
        cleanup();
        scheduleRestart();
    }

    void cleanup()
    {
        if (m_stdoutWatch)
            m_loop->unwatch(m_stdoutWatch);
        if (m_stderrWatch)
            m_loop->unwatch(m_stderrWatch);
        if (m_exitWatch)
            m_loop->unwatch(m_exitWatch);
        m_stdoutWatch = m_stderrWatch = m_exitWatch = 0;
        m_stdoutOpen = m_stderrOpen = false;
        // destroy after the current callback returns
        std::shared_ptr<platform::ChildProcess> process(std::move(m_process));
        std::shared_ptr<StreamCapture> out(std::move(m_stdout));
        std::shared_ptr<StreamCapture> err(std::move(m_stderr));
        m_loop->post([process, out, err] {});
    }

    void scheduleRestart()
    {
        if (m_stopped || !m_params.restartOnExit)
            return;
        m_timer = m_loop->addTimer(std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, m_params.restartDelaySeconds)), [this] {
            m_timer = 0;
            if (!m_stopped)
                launch();
        });
    }

    SourceConfig m_config;
    ProcessParams m_params;
    std::shared_ptr<LegacyCodec> m_legacy;
    std::string m_name;
    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    std::unique_ptr<platform::ChildProcess> m_process;
    std::unique_ptr<StreamCapture> m_stdout;
    std::unique_ptr<StreamCapture> m_stderr;
    bool m_stdoutOpen = false;
    bool m_stderrOpen = false;
    bool m_exited = false;
    bool m_stopped = false;
    bool m_paused = false;
    platform::EventLoop::Id m_stdoutWatch = 0;
    platform::EventLoop::Id m_stderrWatch = 0;
    platform::EventLoop::Id m_exitWatch = 0;
    platform::EventLoop::Id m_timer = 0;
    int64_t m_startTime = 0;
};

// Standard input. POSIX: fd 0 is watched by the event loop.
// Windows: an anonymous pipe or console cannot be waited on, so one blocked reader thread forwards the data.
class StdinSource : public Source
{
public:
    explicit StdinSource(const SourceConfig& config) :
        m_config(config),
        m_decoder(config.encodingValue(), legacyCodecFor(config, true)),
        m_alive(std::make_shared<bool>(true))
    {
    }

    ~StdinSource() override
    {
        // the reader thread may be blocked in a read that never returns; it does not touch this object after m_alive expired
        if (m_thread.joinable())
            m_thread.detach();
    }

    void start(platform::EventLoop& loop, Sink& sink) override
    {
        m_loop = &loop;
        m_sink = &sink;
        m_pid = platform::currentProcessId();
        m_origin = sink.origin(m_config.processNameOverride.empty() ? "stdin" : m_config.processNameOverride);
        setStatus(SourceState::Running, "reading standard input");
#ifdef _WIN32
        std::weak_ptr<bool> alive = m_alive;
        m_thread = std::thread([this, alive, &loop] {
            auto handle = GetStdHandle(STD_INPUT_HANDLE);
            std::vector<char> buffer(64 * 1024);
            for (;;)
            {
                DWORD n = 0;
                bool ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr) && n > 0;
                std::string data = ok ? std::string(buffer.data(), n) : std::string();
                if (alive.expired())
                    return;
                loop.post([this, alive, data = std::move(data), ok] {
                    if (!alive.expired())
                        onData(data, !ok);
                });
                if (!ok)
                    return;
            }
        });
#else
        if (platform::isRegularFile(0))
        {
            // a redirected regular file is always 'readable' and cannot be watched by epoll; read it in slices
            readFileSlice();
            return;
        }
        m_reader = std::make_unique<platform::AsyncReader>(::dup(0));
        m_watch = loop.watch(m_reader->waitHandle(), [this] {
            std::string data;
            auto status = m_reader->read(data);
            onData(data, status == platform::AsyncReader::Status::Eof);
        });
#endif
    }

    void stop(platform::EventLoop& loop) override
    {
        if (m_watch)
            loop.unwatch(m_watch);
        m_watch = 0;
        m_alive.reset();
        setStatus(SourceState::Stopped, "stopped");
    }

    void setPaused(bool paused) override { m_paused = paused; }

private:
#ifndef _WIN32
    void readFileSlice()
    {
        std::string data(256 * 1024, '\0');
        auto n = ::read(0, data.data(), data.size());
        data.resize(n > 0 ? static_cast<size_t>(n) : 0);
        onData(data, n <= 0);
        if (n > 0)
        {
            std::weak_ptr<bool> alive = m_alive;
            m_loop->post([this, alive] {
                if (!alive.expired())
                    readFileSlice();
            });
        }
    }
#endif

    void onData(const std::string& data, bool eof)
    {
        if (!m_paused)
            m_decoder.feed(data, [this](std::string_view line) { m_sink->add(m_pid, m_origin, line); });
        if (eof)
        {
            m_decoder.flush([this](std::string_view line) { m_sink->add(m_pid, m_origin, line); });
            if (m_watch)
                m_loop->unwatch(m_watch);
            m_watch = 0;
            setStatus(SourceState::Stopped, "end of input");
        }
    }

    SourceConfig m_config;
    LineDecoder m_decoder;
    std::shared_ptr<bool> m_alive;
    std::thread m_thread;
    std::unique_ptr<platform::AsyncReader> m_reader;
    platform::EventLoop* m_loop = nullptr;
    Sink* m_sink = nullptr;
    platform::EventLoop::Id m_watch = 0;
    uint32_t m_pid = 0;
    uint32_t m_origin = 0;
    bool m_paused = false;
};

} // namespace

std::unique_ptr<Source> createProcessSource(const SourceConfig& config)
{
    return std::make_unique<ProcessSource>(config);
}

std::unique_ptr<Source> createStdinSource(const SourceConfig& config)
{
    return std::make_unique<StdinSource>(config);
}

} // namespace dv
