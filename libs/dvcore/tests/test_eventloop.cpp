// Conformance tests for the EventLoop backends (win32, linux, macos)

#include "dvcore/Platform.h"

#include <doctest/doctest.h>

#include <fstream>
#include <thread>

using namespace dv;
using namespace std::chrono_literals;

namespace {

auto deadline(std::chrono::milliseconds ms)
{
    return std::chrono::steady_clock::now() + ms;
}

std::filesystem::path tempPath(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / "dvcore_tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}

} // namespace

TEST_CASE("timers fire in order and can be cancelled")
{
    platform::EventLoop loop;
    std::vector<int> fired;
    auto now = std::chrono::steady_clock::now();
    loop.addTimer(now + 30ms, [&] { fired.push_back(2); });
    loop.addTimer(now + 10ms, [&] { fired.push_back(1); });
    auto cancelled = loop.addTimer(now + 20ms, [&] { fired.push_back(99); });
    loop.cancelTimer(cancelled);
    loop.runUntil(deadline(100ms));
    CHECK(fired == std::vector<int>{1, 2});
}

TEST_CASE("post from another thread wakes the loop and stop ends run")
{
    platform::EventLoop loop;
    int value = 0;
    std::thread t([&] {
        std::this_thread::sleep_for(20ms);
        loop.post([&] {
            value = 42;
            loop.stop();
        });
    });
    auto start = std::chrono::steady_clock::now();
    loop.run();
    t.join();
    CHECK(value == 42);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
}

TEST_CASE("readable handles from a child process and process exit")
{
    platform::ChildProcessSpec spec;
#ifdef _WIN32
    spec.program = "cmd.exe";
    spec.arguments = {"/C", "echo hello & exit 3"};
#else
    spec.program = "/bin/sh";
    spec.arguments = {"-c", "echo hello; exit 3"};
#endif
    std::string error;
    auto child = platform::ChildProcess::start(spec, error);
    REQUIRE_MESSAGE(child, error);

    platform::EventLoop loop;
    std::string output;
    bool exited = false;
    bool eof = false;
    platform::EventLoop::Id watch = 0;
    watch = loop.watch(child->stdoutReader().waitHandle(), [&] {
        if (child->stdoutReader().read(output) == platform::AsyncReader::Status::Eof)
        {
            eof = true;
            loop.unwatch(watch);
        }
    });
    loop.watchProcess(child->processRef(), [&] { exited = true; });
    auto end = deadline(10s);
    while ((!exited || !eof) && std::chrono::steady_clock::now() < end)
        loop.runUntil(deadline(50ms));
    CHECK(exited);
    CHECK(eof);
    CHECK(output.find("hello") != std::string::npos);
    CHECK(child->exitDescription() == "exit code 3");
}

TEST_CASE("file watch reports changes")
{
    auto path = tempPath("watched.log");
    {
        std::ofstream out(path);
        out << "start\n";
    }
    platform::EventLoop loop;
    int changes = 0;
    auto id = loop.watchFile(path, [&] { ++changes; });
    REQUIRE(id != 0);
    std::thread writer([&] {
        std::this_thread::sleep_for(50ms);
        std::ofstream out(path, std::ios::app);
        out << "more\n";
    });
    auto end = deadline(5s);
    while (changes == 0 && std::chrono::steady_clock::now() < end)
        loop.runUntil(deadline(50ms));
    writer.join();
    CHECK(changes > 0);
    loop.unwatch(id);
}

TEST_CASE("file identity changes on replace")
{
    auto path = tempPath("identity.log");
    {
        std::ofstream out(path);
        out << "a\n";
    }
    auto id1 = platform::File::idOf(path);
    REQUIRE(id1);
    std::string error;
    auto open = platform::File::open(path, error);
    REQUIRE(open);
    CHECK(open->id() == id1);
    auto moved = tempPath("identity.log.1");
    std::filesystem::remove(moved);
    std::filesystem::rename(path, moved); // allowed while open (share-delete on Windows)
    {
        std::ofstream out(path);
        out << "b\n";
    }
    auto id2 = platform::File::idOf(path);
    REQUIRE(id2);
    CHECK_FALSE(*id1 == *id2);
}
