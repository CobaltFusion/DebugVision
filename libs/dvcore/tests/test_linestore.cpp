#include "dvcore/LineStore.h"
#include "dvcore/LogView.h"
#include "dvcore/Source.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>

using namespace dv;

TEST_CASE("LineStore append and read")
{
    LineStore store;
    auto origin = store.origins().intern(1, "app.exe");
    store.append(10, 20, 42, origin, "hello");
    store.append(11, 21, 42, origin, "world");
    CHECK(store.size() == 2);
    std::vector<std::string> texts;
    store.forEach(0, store.endSeq(), [&](const LineRef& line) { texts.emplace_back(line.text); });
    CHECK(texts == std::vector<std::string>{"hello", "world"});
    LineRecord rec{};
    std::string text;
    REQUIRE(store.get(1, rec, text));
    CHECK(rec.pid == 42);
    CHECK(store.origins().get(rec.originId).processName == "app.exe");
}

TEST_CASE("LineStore spans chunks and enforces the history limit")
{
    LineStore store(Chunk::maxLines + 10);
    for (uint32_t i = 0; i < Chunk::maxLines * 3; ++i)
        store.append(i, i, 0, 0, "x");
    CHECK(store.endSeq() == Chunk::maxLines * 3);
    // whole chunks are dropped, at least 'limit' lines remain
    CHECK(store.size() >= Chunk::maxLines + 10);
    CHECK(store.size() <= 2 * Chunk::maxLines + 10);
    uint64_t count = 0;
    store.forEach(0, store.endSeq(), [&](const LineRef&) { ++count; });
    CHECK(count == store.size());
}

TEST_CASE("LineStore clear and crop")
{
    LineStore store;
    for (int i = 0; i < 100; ++i)
        store.append(i, i, 0, 0, "line");
    auto gen = store.clearGeneration();
    store.crop(40);
    CHECK(store.beginSeq() == 40);
    CHECK(store.clearGeneration() != gen);
    store.clear();
    CHECK(store.size() == 0);
    store.append(0, 0, 0, 0, "after");
    CHECK(store.size() == 1);
}

TEST_CASE("LineStore concurrent reader while writing and evicting")
{
    LineStore store(Chunk::maxLines);
    std::atomic<bool> done{false};
    std::thread reader([&] {
        while (!done)
        {
            auto end = store.endSeq();
            auto begin = end > 1000 ? end - 1000 : 0;
            store.forEach(begin, end, [](const LineRef& line) { REQUIRE(line.text == "payload"); });
        }
    });
    for (uint32_t i = 0; i < Chunk::maxLines * 4; ++i)
        store.append(i, i, 0, 0, "payload");
    done = true;
    reader.join();
    CHECK(store.endSeq() == Chunk::maxLines * 4);
}

TEST_CASE("Sink handles DBGVIEWCLEAR")
{
    LineStore store;
    Sink sink(store, 1);
    sink.add(0, 0, "a");
    sink.add(0, 0, "b");
    sink.add(0, 0, "DBGVIEWCLEAR");
    CHECK(store.size() == 0);
    sink.add(0, 0, "c");
    CHECK(store.size() == 1);
}

TEST_CASE("MessageAssembler joins partial messages per process without auto newline")
{
    LineStore store;
    Sink sink(store, 1);
    MessageAssembler assembler(Encoding::Utf8OrLegacy, nullptr, false);
    assembler.add(sink, 0, 0, 1, 0, "hel");
    assembler.add(sink, 0, 0, 2, 0, "other\n");
    assembler.add(sink, 0, 0, 1, 0, "lo\nnext");
    CHECK(store.size() == 2);
    assembler.flush(sink, 1);
    std::vector<std::string> texts;
    store.forEach(0, store.endSeq(), [&](const LineRef& line) { texts.emplace_back(line.text); });
    CHECK(texts == std::vector<std::string>{"other", "hello", "next"});
}

TEST_CASE("FilterWorker filters new lines and re-filters")
{
    LineStore store;
    FilterWorker worker(store);
    std::atomic<int> notified{0};
    worker.setNotify([&] { ++notified; });
    worker.start();
    FilterSet filters;
    filters.messageFilters.push_back(Filter{"error", MatchType::Simple, FilterType::Include});
    auto view = worker.addView(filters);
    auto origin = store.origins().intern(1, "p");
    store.append(0, 0, 1, origin, "an ERROR here");
    store.append(0, 0, 1, origin, "fine");
    store.append(0, 0, 1, origin, "error again");
    for (int i = 0; i < 200 && view->size() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(view->size() == 2);

    view->setFilters(FilterSet{});
    worker.wake();
    for (int i = 0; i < 200 && view->size() < 3; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(view->size() == 3);
    CHECK(notified > 0);
    worker.stop();
}
