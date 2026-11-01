#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dv {

// Everything stored in the LineStore is valid UTF-8; decoding happens once, at the ingest boundary.
enum class Encoding
{
    Auto,         // detect from the first bytes (BOM, UTF-16 heuristic), otherwise Utf8OrLegacy
    Utf8,         // strict UTF-8, invalid sequences become U+FFFD
    Utf8OrLegacy, // per line: valid UTF-8 is taken as-is, anything else is decoded with the legacy codec
    Legacy,       // always decode with the legacy codec (ANSI/OEM code page or iconv charset)
    Utf16LE,
    Utf16BE,
};

const char* toText(Encoding encoding);
std::optional<Encoding> parseEncoding(std::string_view text);

struct DetectedEncoding
{
    Encoding encoding;
    size_t bomLength;
};

// Detects the encoding of the start of a stream:
//  - BOM: UTF-8 / UTF-16LE / UTF-16BE
//  - no BOM: NUL-byte parity heuristic for UTF-16, otherwise Utf8OrLegacy
DetectedEncoding detectEncoding(std::string_view sample);

bool isAscii(std::string_view text);
bool isValidUtf8(std::string_view text);
// appends 'text' to 'out', replacing invalid UTF-8 sequences with U+FFFD
void appendSanitizedUtf8(std::string_view text, std::string& out);
void appendUtf8(char32_t codepoint, std::string& out);

// Converts text in a legacy (8-bit or DBCS) charset to UTF-8.
// Implemented per platform: MultiByteToWideChar on Windows, iconv on POSIX.
class LegacyCodec
{
public:
    virtual ~LegacyCodec() = default;
    virtual std::string name() const = 0;
    // appends the UTF-8 conversion of 'text' to 'out'
    virtual void toUtf8(std::string_view text, std::string& out) = 0;

    // charset: "" = platform default (Windows: the ANSI code page, POSIX: windows-1252),
    // "oem" = Windows OEM code page, "cp1252" / "windows-1252" / "1252" / any iconv name
    static std::unique_ptr<LegacyCodec> create(std::string_view charset);
};

// Incremental UTF-16 (LE/BE) to UTF-8 decoder, keeps an odd trailing byte and a pending high surrogate across calls.
// Unpaired surrogates (as written by UCS-2 producers) become U+FFFD.
class Utf16Decoder
{
public:
    explicit Utf16Decoder(bool bigEndian = false) :
        m_bigEndian(bigEndian)
    {
    }

    void decode(std::string_view bytes, std::string& out);
    void flush(std::string& out); // emits U+FFFD for a dangling high surrogate or odd byte
    void reset();

private:
    void decodeUnit(char16_t unit, std::string& out);

    bool m_bigEndian;
    bool m_hasOddByte = false;
    unsigned char m_oddByte = 0;
    char16_t m_highSurrogate = 0;
};

// Converts a byte stream into UTF-8 lines.
//  - lines end at '\n'; a trailing '\r' is removed
//  - UTF-16 is decoded before splitting (the byte 0x0A can occur inside a UTF-16 code unit)
//  - 8-bit encodings are split first and decoded per line (0x0A never occurs inside UTF-8 or DBCS sequences)
//  - lines longer than maxLineLength are emitted in pieces
class LineDecoder
{
public:
    static constexpr size_t defaultMaxLineLength = 64 * 1024;

    explicit LineDecoder(Encoding encoding = Encoding::Auto, std::shared_ptr<LegacyCodec> legacy = {}, size_t maxLineLength = defaultMaxLineLength);

    Encoding encoding() const { return m_encoding; }
    void setEncoding(Encoding encoding);
    void setLegacyCodec(std::shared_ptr<LegacyCodec> legacy) { m_legacy = std::move(legacy); }

    // emit(std::string_view utf8Line) is called for every complete line
    template <typename Emit>
    void feed(std::string_view bytes, Emit&& emit)
    {
        if (bytes.empty())
            return;
        std::string detectBuffer;
        if (m_encoding == Encoding::Auto)
        {
            // wait for enough bytes to recognize a BOM or UTF-16
            m_detect.append(bytes);
            if (m_detect.size() < minDetectBytes)
                return;
            detectBuffer.swap(m_detect);
            bytes = detectBuffer;
            resolveAuto(bytes);
        }
        if (m_encoding == Encoding::Utf16LE || m_encoding == Encoding::Utf16BE)
        {
            m_utf16Out.clear();
            m_utf16.decode(bytes, m_utf16Out);
            split(m_utf16Out, emit);
        }
        else
        {
            split(bytes, emit);
        }
    }

    // emits the pending partial line, if any
    template <typename Emit>
    void flush(Emit&& emit)
    {
        if (m_encoding == Encoding::Auto && !m_detect.empty())
        {
            std::string pending;
            pending.swap(m_detect);
            std::string_view bytes = pending;
            resolveAuto(bytes);
            feed(bytes, emit);
        }
        if (m_encoding == Encoding::Utf16LE || m_encoding == Encoding::Utf16BE)
        {
            m_utf16Out.clear();
            m_utf16.flush(m_utf16Out);
            m_partial.append(m_utf16Out);
        }
        if (!m_partial.empty())
        {
            emitLine(m_partial, emit);
            m_partial.clear();
        }
    }

    bool hasPartial() const { return !m_partial.empty(); }
    void reset(); // drops partial data and decoder state, re-detects when the encoding was Auto

    // decodes one raw (already split) line according to the 8-bit encoding mode; the result is valid UTF-8
    std::string_view decodeLine(std::string_view raw);

private:
    void resolveAuto(std::string_view& bytes);

    template <typename Emit>
    void split(std::string_view bytes, Emit& emit)
    {
        while (!bytes.empty())
        {
            auto nl = static_cast<const char*>(std::memchr(bytes.data(), '\n', bytes.size()));
            if (!nl)
            {
                m_partial.append(bytes);
                while (m_partial.size() >= m_maxLineLength)
                {
                    auto cut = utf8SafeCut(m_partial, m_maxLineLength);
                    emitLine(std::string_view(m_partial).substr(0, cut), emit);
                    m_partial.erase(0, cut);
                }
                return;
            }
            auto len = static_cast<size_t>(nl - bytes.data());
            if (m_partial.empty())
            {
                emitLine(bytes.substr(0, len), emit);
            }
            else
            {
                m_partial.append(bytes.data(), len);
                emitLine(m_partial, emit);
                m_partial.clear();
            }
            bytes.remove_prefix(len + 1);
        }
    }

    template <typename Emit>
    void emitLine(std::string_view raw, Emit& emit)
    {
        if (!raw.empty() && raw.back() == '\r')
            raw.remove_suffix(1);
        emit(decodeLine(raw));
    }

    static size_t utf8SafeCut(std::string_view text, size_t max);
    static constexpr size_t minDetectBytes = 4;

    Encoding m_encoding;
    bool m_wasAuto;
    std::shared_ptr<LegacyCodec> m_legacy;
    size_t m_maxLineLength;
    Utf16Decoder m_utf16;
    std::string m_utf16Out;
    std::string m_partial;
    std::string m_decoded;
    std::string m_detect; // bytes received before the encoding was detected
};

} // namespace dv
