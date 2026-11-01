#include "dvcore/Encoding.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace dv;

namespace {

std::string utf16le(std::u16string_view text, bool bom = false)
{
    std::string out;
    if (bom)
        out += "\xFF\xFE";
    for (auto c : text)
    {
        out.push_back(static_cast<char>(c & 0xff));
        out.push_back(static_cast<char>(c >> 8));
    }
    return out;
}

std::string utf16be(std::u16string_view text)
{
    std::string out;
    for (auto c : text)
    {
        out.push_back(static_cast<char>(c >> 8));
        out.push_back(static_cast<char>(c & 0xff));
    }
    return out;
}

// feeds 'bytes' split at 'cut' and returns all lines including the flushed partial line
std::vector<std::string> decodeSplit(const std::string& bytes, size_t cut, Encoding encoding = Encoding::Auto)
{
    LineDecoder decoder(encoding, LegacyCodec::create("windows-1252"));
    std::vector<std::string> lines;
    auto emit = [&](std::string_view line) { lines.emplace_back(line); };
    decoder.feed(std::string_view(bytes).substr(0, cut), emit);
    decoder.feed(std::string_view(bytes).substr(cut), emit);
    decoder.flush(emit);
    return lines;
}

const std::string emoji = "\xF0\x9F\x98\xB1";        // U+1F631
const std::string cjk = "\xE3\x83\x91\xE3\x83\xBC";  // パー
const std::u16string emoji16 = u"\U0001F631";

} // namespace

TEST_CASE("detectEncoding")
{
    CHECK(detectEncoding("\xEF\xBB\xBFhello").encoding == Encoding::Utf8);
    CHECK(detectEncoding("\xEF\xBB\xBFhello").bomLength == 3);
    CHECK(detectEncoding(utf16le(u"hello", true)).encoding == Encoding::Utf16LE);
    CHECK(detectEncoding(std::string_view("\xFE\xFF\0h", 4)).encoding == Encoding::Utf16BE);
    CHECK(detectEncoding(utf16le(u"hello world\r\n")).encoding == Encoding::Utf16LE);
    CHECK(detectEncoding(utf16be(u"hello world\r\n")).encoding == Encoding::Utf16BE);
    CHECK(detectEncoding("plain ascii text\n").encoding == Encoding::Utf8OrLegacy);
}

TEST_CASE("UTF-8 validation and sanitizing")
{
    CHECK(isAscii("hello"));
    CHECK_FALSE(isAscii("h\xC3\xA9llo"));
    CHECK(isValidUtf8("h\xC3\xA9llo " + emoji + cjk));
    CHECK_FALSE(isValidUtf8("\xC3"));
    CHECK_FALSE(isValidUtf8("\xED\xA0\x80")); // surrogate
    CHECK_FALSE(isValidUtf8("\xC0\xAF"));     // overlong
    std::string out;
    appendSanitizedUtf8("a\xFF" "b", out);
    CHECK(out == "a\xEF\xBF\xBD" "b");
}

TEST_CASE("UTF-16 decoder handles splits, surrogates and lone surrogates")
{
    auto bytes = utf16le(u"A" + emoji16 + u"B");
    for (size_t cut = 0; cut <= bytes.size(); ++cut)
    {
        Utf16Decoder d;
        std::string out;
        d.decode(std::string_view(bytes).substr(0, cut), out);
        d.decode(std::string_view(bytes).substr(cut), out);
        d.flush(out);
        CHECK(out == "A" + emoji + "B");
    }
    Utf16Decoder d;
    std::string out;
    d.decode(utf16le(u"x\xD800y"), out); // lone high surrogate (UCS-2 data)
    CHECK(out == "x\xEF\xBF\xBDy");
}

TEST_CASE("LineDecoder splits UTF-16 lines at every byte boundary")
{
    auto text = u"first " + emoji16 + u"\r\nsecond āĊ line\nthird";
    auto bytes = utf16le(text, true);
    for (size_t cut = 0; cut <= bytes.size(); ++cut)
    {
        auto lines = decodeSplit(bytes, cut);
        REQUIRE(lines.size() == 3);
        CHECK(lines[0] == "first " + emoji);
        CHECK(lines[1] == "second \xC4\x81\xC4\x8A line"); // U+010A contains the byte 0x0A
        CHECK(lines[2] == "third");
    }
}

TEST_CASE("LineDecoder big endian")
{
    auto lines = decodeSplit(std::string("\xFE\xFF", 2) + utf16be(u"be text\n" + emoji16 + u"\n"), 5);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "be text");
    CHECK(lines[1] == emoji);
}

TEST_CASE("LineDecoder UTF-8 lines at every byte boundary")
{
    std::string bytes = "one " + emoji + "\ntwo " + cjk + "\r\nthree";
    for (size_t cut = 0; cut <= bytes.size(); ++cut)
    {
        auto lines = decodeSplit(bytes, cut);
        REQUIRE(lines.size() == 3);
        CHECK(lines[0] == "one " + emoji);
        CHECK(lines[1] == "two " + cjk);
        CHECK(lines[2] == "three");
    }
}

TEST_CASE("Utf8OrLegacy: valid UTF-8 wins, other lines use the legacy codec")
{
    auto lines = decodeSplit("caf\xC3\xA9\ncaf\xE9\n", 0, Encoding::Utf8OrLegacy);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "caf\xC3\xA9");
    CHECK(lines[1] == "caf\xC3\xA9"); // windows-1252 0xE9 = é
}

TEST_CASE("strict UTF-8 replaces invalid bytes")
{
    auto lines = decodeSplit("\xEF\xBB\xBF" "bad\xFF\n", 0);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "bad\xEF\xBF\xBD");
}

TEST_CASE("long lines are emitted in pieces without breaking UTF-8 sequences")
{
    LineDecoder decoder(Encoding::Utf8, nullptr, 10);
    std::vector<std::string> lines;
    decoder.feed("abcdefgh" + emoji + emoji, [&](std::string_view l) { lines.emplace_back(l); });
    decoder.flush([&](std::string_view l) { lines.emplace_back(l); });
    std::string joined;
    for (auto& l : lines)
    {
        CHECK(isValidUtf8(l));
        joined += l;
    }
    CHECK(joined == "abcdefgh" + emoji + emoji);
}
