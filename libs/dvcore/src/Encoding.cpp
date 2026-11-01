#include "dvcore/Encoding.h"

#include <algorithm>
#include <array>

namespace dv {

namespace {

struct EncodingName
{
    Encoding encoding;
    const char* name;
};

constexpr std::array encodingNames = {
    EncodingName{Encoding::Auto, "auto"},
    EncodingName{Encoding::Utf8, "utf-8"},
    EncodingName{Encoding::Utf8OrLegacy, "utf-8-or-legacy"},
    EncodingName{Encoding::Legacy, "legacy"},
    EncodingName{Encoding::Utf16LE, "utf-16le"},
    EncodingName{Encoding::Utf16BE, "utf-16be"},
};

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        auto lx = (x >= 'A' && x <= 'Z') ? static_cast<char>(x - 'A' + 'a') : x;
        auto ly = (y >= 'A' && y <= 'Z') ? static_cast<char>(y - 'A' + 'a') : y;
        return lx == ly;
    });
}

constexpr std::string_view replacementCharacter = "\xEF\xBF\xBD";

// returns the length of the valid UTF-8 sequence starting at p, or 0 when invalid
size_t utf8SequenceLength(const unsigned char* p, const unsigned char* end)
{
    auto c = p[0];
    if (c < 0x80)
        return 1;
    auto avail = static_cast<size_t>(end - p);
    if (c >= 0xC2 && c <= 0xDF)
    {
        return (avail >= 2 && (p[1] & 0xC0) == 0x80) ? 2 : 0;
    }
    if (c >= 0xE0 && c <= 0xEF)
    {
        if (avail < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80)
            return 0;
        if (c == 0xE0 && p[1] < 0xA0)
            return 0; // overlong
        if (c == 0xED && p[1] >= 0xA0)
            return 0; // surrogate
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4)
    {
        if (avail < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80)
            return 0;
        if (c == 0xF0 && p[1] < 0x90)
            return 0; // overlong
        if (c == 0xF4 && p[1] >= 0x90)
            return 0; // > U+10FFFF
        return 4;
    }
    return 0;
}

} // namespace

const char* toText(Encoding encoding)
{
    for (auto& e : encodingNames)
        if (e.encoding == encoding)
            return e.name;
    return "auto";
}

std::optional<Encoding> parseEncoding(std::string_view text)
{
    for (auto& e : encodingNames)
        if (equalsIgnoreCase(text, e.name))
            return e.encoding;
    if (equalsIgnoreCase(text, "utf8"))
        return Encoding::Utf8;
    if (equalsIgnoreCase(text, "utf16le") || equalsIgnoreCase(text, "utf-16") || equalsIgnoreCase(text, "ucs-2"))
        return Encoding::Utf16LE;
    if (equalsIgnoreCase(text, "utf16be"))
        return Encoding::Utf16BE;
    return std::nullopt;
}

DetectedEncoding detectEncoding(std::string_view sample)
{
    auto bytes = reinterpret_cast<const unsigned char*>(sample.data());
    auto size = sample.size();
    if (size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        return {Encoding::Utf8, 3};
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
        return {Encoding::Utf16LE, 2};
    if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
        return {Encoding::Utf16BE, 2};

    // UTF-16 without BOM: mostly-ASCII text has a NUL in every other byte
    size = std::min<size_t>(size, 64 * 1024) & ~size_t(1);
    if (size >= 4)
    {
        size_t evenNuls = 0;
        size_t oddNuls = 0;
        for (size_t i = 0; i < size; i += 2)
        {
            evenNuls += bytes[i] == 0;
            oddNuls += bytes[i + 1] == 0;
        }
        auto units = size / 2;
        if (oddNuls * 100 > units * 40 && evenNuls * 100 < units * 5)
            return {Encoding::Utf16LE, 0};
        if (evenNuls * 100 > units * 40 && oddNuls * 100 < units * 5)
            return {Encoding::Utf16BE, 0};
    }
    return {Encoding::Utf8OrLegacy, 0};
}

bool isAscii(std::string_view text)
{
    auto p = text.data();
    auto n = text.size();
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        uint64_t word;
        std::memcpy(&word, p + i, 8);
        if (word & 0x8080808080808080ull)
            return false;
    }
    for (; i < n; ++i)
        if (static_cast<unsigned char>(p[i]) & 0x80)
            return false;
    return true;
}

bool isValidUtf8(std::string_view text)
{
    auto p = reinterpret_cast<const unsigned char*>(text.data());
    auto end = p + text.size();
    while (p < end)
    {
        if (*p < 0x80)
        {
            ++p;
            continue;
        }
        auto len = utf8SequenceLength(p, end);
        if (len == 0)
            return false;
        p += len;
    }
    return true;
}

void appendSanitizedUtf8(std::string_view text, std::string& out)
{
    auto begin = reinterpret_cast<const unsigned char*>(text.data());
    auto p = begin;
    auto end = p + text.size();
    auto runStart = p;
    while (p < end)
    {
        if (*p < 0x80)
        {
            ++p;
            continue;
        }
        auto len = utf8SequenceLength(p, end);
        if (len == 0)
        {
            out.append(text.data() + (runStart - begin), static_cast<size_t>(p - runStart));
            out.append(replacementCharacter);
            ++p;
            runStart = p;
            continue;
        }
        p += len;
    }
    out.append(text.data() + (runStart - begin), static_cast<size_t>(p - runStart));
}

void appendUtf8(char32_t cp, std::string& out)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void Utf16Decoder::decodeUnit(char16_t unit, std::string& out)
{
    if (m_highSurrogate != 0)
    {
        if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            char32_t cp = 0x10000 + ((static_cast<char32_t>(m_highSurrogate) - 0xD800) << 10) + (static_cast<char32_t>(unit) - 0xDC00);
            m_highSurrogate = 0;
            appendUtf8(cp, out);
            return;
        }
        // unpaired high surrogate
        m_highSurrogate = 0;
        out.append(replacementCharacter);
    }
    if (unit >= 0xD800 && unit <= 0xDBFF)
    {
        m_highSurrogate = unit;
        return;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF)
    {
        out.append(replacementCharacter); // unpaired low surrogate
        return;
    }
    appendUtf8(unit, out);
}

void Utf16Decoder::decode(std::string_view bytes, std::string& out)
{
    auto p = reinterpret_cast<const unsigned char*>(bytes.data());
    auto n = bytes.size();
    size_t i = 0;
    auto unitAt = [this](unsigned char b0, unsigned char b1) {
        return m_bigEndian ? static_cast<char16_t>((b0 << 8) | b1) : static_cast<char16_t>((b1 << 8) | b0);
    };
    if (m_hasOddByte && n > 0)
    {
        decodeUnit(unitAt(m_oddByte, p[0]), out);
        m_hasOddByte = false;
        i = 1;
    }
    out.reserve(out.size() + (n - i) / 2);
    for (; i + 1 < n; i += 2)
    {
        auto unit = unitAt(p[i], p[i + 1]);
        if (unit < 0x80 && m_highSurrogate == 0)
            out.push_back(static_cast<char>(unit)); // ASCII fast path
        else
            decodeUnit(unit, out);
    }
    if (i < n)
    {
        m_hasOddByte = true;
        m_oddByte = p[i];
    }
}

void Utf16Decoder::flush(std::string& out)
{
    if (m_highSurrogate != 0 || m_hasOddByte)
        out.append(replacementCharacter);
    m_highSurrogate = 0;
    m_hasOddByte = false;
}

void Utf16Decoder::reset()
{
    m_highSurrogate = 0;
    m_hasOddByte = false;
}

LineDecoder::LineDecoder(Encoding encoding, std::shared_ptr<LegacyCodec> legacy, size_t maxLineLength) :
    m_encoding(encoding),
    m_wasAuto(encoding == Encoding::Auto),
    m_legacy(std::move(legacy)),
    m_maxLineLength(maxLineLength)
{
    setEncoding(encoding);
}

void LineDecoder::setEncoding(Encoding encoding)
{
    m_encoding = encoding;
    m_utf16 = Utf16Decoder(encoding == Encoding::Utf16BE);
}

void LineDecoder::reset()
{
    m_partial.clear();
    m_detect.clear();
    m_utf16.reset();
    if (m_wasAuto)
        m_encoding = Encoding::Auto;
}

void LineDecoder::resolveAuto(std::string_view& bytes)
{
    auto detected = detectEncoding(bytes);
    setEncoding(detected.encoding);
    bytes.remove_prefix(detected.bomLength);
}

std::string_view LineDecoder::decodeLine(std::string_view raw)
{
    if (isAscii(raw))
        return raw;
    switch (m_encoding)
    {
    case Encoding::Legacy:
        if (m_legacy)
        {
            m_decoded.clear();
            m_legacy->toUtf8(raw, m_decoded);
            return m_decoded;
        }
        break;
    case Encoding::Utf8OrLegacy:
    case Encoding::Auto:
        if (isValidUtf8(raw))
            return raw;
        if (m_legacy)
        {
            m_decoded.clear();
            m_legacy->toUtf8(raw, m_decoded);
            return m_decoded;
        }
        break;
    case Encoding::Utf8:
    case Encoding::Utf16LE: // already converted to UTF-8 by the Utf16Decoder
    case Encoding::Utf16BE:
        if (isValidUtf8(raw))
            return raw;
        break;
    }
    m_decoded.clear();
    appendSanitizedUtf8(raw, m_decoded);
    return m_decoded;
}

size_t LineDecoder::utf8SafeCut(std::string_view text, size_t max)
{
    // do not cut inside a multi-byte sequence
    auto cut = std::min(max, text.size());
    auto limit = cut > 3 ? cut - 3 : 0;
    while (cut > limit && cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
    return cut == 0 ? std::min(max, text.size()) : cut;
}

} // namespace dv
