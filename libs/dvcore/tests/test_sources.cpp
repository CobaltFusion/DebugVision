// End-to-end tests: sources running in the Ingest thread, writing into a LineStore

#include "dvcore/Ingest.h"
#include "dvcore/LineStore.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <thread>

using namespace dv;
using namespace std::chrono_literals;

namespace {

std::filesystem::path tempPath(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / "dvcore_tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}

std::vector<std::string> messages(const LineStore& store, bool skipInternal = true)
{
    std::vector<std::string> result;
    store.forEach(store.beginSeq(), store.endSeq(), [&](const LineRef& line) {
        if (skipInternal && store.origins().get(line.record->originId).processName == "[internal]")
            return;
        result.emplace_back(line.text);
    });
    return result;
}

template <typename Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout = 5s)
{
    auto end = std::chrono::steady_clock::now() + timeout;
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(10ms);
    }
    return true;
}

void append(const std::filesystem::path& path, const std::string& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << data;
}

} // namespace

TEST_CASE("file source: tail, partial line, truncation and rotation")
{
    auto path = tempPath("tail.log");
    std::filesystem::remove(path);
    std::filesystem::remove(tempPath("tail.log.1"));
    append(path, "line 1\nline 2\npart");

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    auto config = SourceRegistry::instance().parseSpec("file:path=" + platform::pathToUtf8(path));
    ingest.addSource(config);

    CHECK(waitFor([&] { return messages(store).size() == 2; }));
    append(path, "ial\nline 4\n");
    CHECK(waitFor([&] { return messages(store).size() == 4; }));
    CHECK(messages(store)[2] == "partial");

    // truncation: the file is read again from the start
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "new\n";
    }
    CHECK(waitFor([&] { return messages(store).size() == 5; }));
    CHECK(messages(store).back() == "new");

    // rotation: rename and recreate
    std::filesystem::rename(path, tempPath("tail.log.1"));
    append(path, "rotated\n");
    CHECK(waitFor([&] { return messages(store).size() == 6; }));
    CHECK(messages(store).back() == "rotated");
    ingest.stop();
}

TEST_CASE("file source: UTF-16 file is tailed")
{
    auto path = tempPath("utf16.log");
    std::filesystem::remove(path);
    append(path, std::string("\xFF\xFE" "a\0b\0\n\0", 8));

    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(SourceRegistry::instance().parseSpec("file:path=" + platform::pathToUtf8(path)));
    CHECK(waitFor([&] { return messages(store).size() == 1; }));
    append(path, std::string("c\0\x01\x01\n\0", 6)); // "c" U+0101
    CHECK(waitFor([&] { return messages(store).size() == 2; }));
    CHECK(messages(store) == std::vector<std::string>{"ab", "c\xC4\x81"});
    ingest.stop();
}

TEST_CASE("file source: missing file is waited for")
{
    auto path = tempPath("later.log");
    std::filesystem::remove(path);
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(SourceRegistry::instance().parseSpec("file:path=" + platform::pathToUtf8(path)));
    std::this_thread::sleep_for(100ms);
    append(path, "finally\n");
    CHECK(waitFor([&] { return messages(store).size() == 1; }));
    ingest.stop();
}

TEST_CASE("process source captures stdout and stderr and reports the exit")
{
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    auto config = SourceRegistry::instance().makeDefault("process");
    ProcessParams params;
#ifdef _WIN32
    params.command = "cmd.exe";
    params.arguments = {"/C", "echo out & echo err 1>&2 & exit 5"};
#else
    params.command = "/bin/sh";
    params.arguments = {"-c", "echo out; echo err 1>&2; exit 5"};
#endif
    config.setParams(params);
    ingest.addSource(config);
    CHECK(waitFor([&] {
        for (auto& m : messages(store, false))
            if (m.find("terminated") != std::string::npos)
                return true;
        return false;
    }));
    auto all = messages(store, false);
    bool sawOut = false;
    bool sawErr = false;
    bool sawExit = false;
    for (auto& m : all)
    {
        sawOut = sawOut || m.rfind("out", 0) == 0;
        sawErr = sawErr || m.rfind("err", 0) == 0;
        sawExit = sawExit || m.find("exit code 5") != std::string::npos;
    }
    CHECK(sawOut);
    CHECK(sawErr);
    CHECK(sawExit);
    ingest.stop();
}

TEST_CASE("udp source receives datagrams and reconfiguration restarts only changed sources")
{
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    auto udp = SourceRegistry::instance().parseSpec("udp:port=34567,bind=127.0.0.1");
    auto filePath = tempPath("keep.log");
    std::filesystem::remove(filePath);
    append(filePath, "kept\n");
    auto file = SourceRegistry::instance().parseSpec("file:path=" + platform::pathToUtf8(filePath));
    ingest.applySources({udp, file});
    CHECK(waitFor([&] { return !ingest.sources().empty() && ingest.sources()[0].status.state == SourceState::Running; }));

    std::string error;
    CHECK(platform::sendUdp("127.0.0.1", 34567, "hello udp\nsecond line", error));
    CHECK(waitFor([&] { return messages(store).size() == 3; }));

    // change the UDP port: the file source keeps its offset, so 'kept' is not read twice
    auto udp2 = udp;
    udp2.setParams(UdpParams{"127.0.0.1", 34568});
    ingest.applySources({udp2, file});
    CHECK(waitFor([&] { return !ingest.sources().empty() && ingest.sources()[0].status.text.find("34568") != std::string::npos; }));
    CHECK(platform::sendUdp("127.0.0.1", 34568, "on new port", error));
    CHECK(waitFor([&] { return messages(store).size() == 4; }));
    auto m = messages(store);
    CHECK(std::count(m.begin(), m.end(), "kept") == 1);

    // a second UDP source on the same port reports the error as its status
    auto clash = SourceRegistry::instance().parseSpec("udp:port=34568,bind=127.0.0.1");
    ingest.applySources({udp2, file, clash});
    CHECK(waitFor([&] { return ingest.sources().size() == 3 && ingest.sources()[2].status.state == SourceState::Error; }));
    ingest.stop();
}

TEST_CASE("udp sender filter")
{
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(SourceRegistry::instance().parseSpec("udp:port=34569,bind=127.0.0.1,allowedSenders=10.0.0.0/8"));
    CHECK(waitFor([&] { return !ingest.sources().empty() && ingest.sources()[0].status.state == SourceState::Running; }));
    std::string error;
    platform::sendUdp("127.0.0.1", 34569, "blocked", error);
    std::this_thread::sleep_for(200ms);
    CHECK(messages(store).empty());
    ingest.stop();
}

TEST_CASE("udp sender: one port for all datagrams, one process by default")
{
    LineStore store;
    Ingest ingest(store);
    ingest.start();
    ingest.addSource(SourceRegistry::instance().parseSpec("udp:port=34570,bind=127.0.0.1"));
    CHECK(waitFor([&] { return !ingest.sources().empty() && ingest.sources()[0].status.state == SourceState::Running; }));
    std::string error;
    auto sender = platform::UdpSender::open("127.0.0.1", 34570, error);
    REQUIRE(sender);
    CHECK(sender->send("first", error));
    CHECK(sender->send("second", error));
    CHECK(waitFor([&] { return messages(store).size() == 2; }));
    // the process name contains the sender's port, so both lines have the same process
    std::vector<std::string> processes;
    store.forEach(store.beginSeq(), store.endSeq(), [&](const LineRef& line) {
        auto& name = store.origins().get(line.record->originId).processName;
        if (name != "[internal]")
            processes.push_back(name);
    });
    REQUIRE(processes.size() == 2);
    CHECK(processes[0] == processes[1]);
    CHECK(processes[0] == "[UDP 127.0.0.1]"); // the default process name has no port
    // a sender that opens a socket per datagram: still one process by default
    CHECK(platform::sendUdp("127.0.0.1", 34570, "third", error));
    CHECK(waitFor([&] { return messages(store).size() == 3; }));
    store.forEach(store.endSeq() - 1, store.endSeq(), [&](const LineRef& line) { CHECK(store.origins().get(line.record->originId).processName == "[UDP 127.0.0.1]"); });
    ingest.stop();
}
