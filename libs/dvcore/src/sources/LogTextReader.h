#pragma once

// Turns the bytes of a log file into lines for a Sink: detects the encoding and the log format (text, dblog, sysinternals)
// from the start of the file and parses the time stamps, pids and process names of the dblog and sysinternals formats.
// Shared by the file and HTTP sources; all methods are called on the ingest thread.

#include "dvcore/Clock.h"
#include "dvcore/Encoding.h"
#include "dvcore/FileFormat.h"
#include "dvcore/Source.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dv {

class LogTextReader
{
public:
    // format: auto | text | dblog | sysinternals
    LogTextReader(Encoding configuredEncoding, std::shared_ptr<LegacyCodec> legacy, std::string format) :
        m_configuredEncoding(configuredEncoding),
        m_legacy(std::move(legacy)),
        m_formatSetting(std::move(format))
    {
    }

    void attach(Sink& sink, uint32_t origin)
    {
        m_sink = &sink;
        m_origin = origin;
        m_baseSteadyNs = nowSteadyNs();
    }

    // (re)starts reading a file from 'sample', the first bytes of the file; 'extension' (e.g. ".dblog") helps to recognize
    // the format. Returns the number of bytes to skip at the start of the file (a byte order mark).
    size_t begin(std::string_view sample, std::string_view extension)
    {
        auto detected = detectEncoding(sample);
        auto encoding = m_configuredEncoding == Encoding::Auto ? detected.encoding : m_configuredEncoding;
        m_decoder = LineDecoder(encoding, m_legacy);
        m_format = detectFormat(sample, encoding, extension);
        m_firstLine = m_format == LogFormat::DbLog;
        m_sysinternals = SysinternalsParser{};
        return detected.encoding == encoding ? detected.bomLength : 0;
    }

    Encoding encoding() const { return m_decoder.encoding(); }
    LogFormat format() const { return m_format; }

    // e.g. "utf-8, dblog"
    std::string description() const
    {
        return std::string(toText(encoding())) + (m_format != LogFormat::Text ? std::string(", ") + toText(m_format) : std::string());
    }

    void feed(std::string_view bytes)
    {
        m_decoder.feed(bytes, [this](std::string_view line) { addLine(line); });
    }

    // emits a last line without a newline
    void flush()
    {
        m_decoder.flush([this](std::string_view line) { addLine(line); });
    }

    // the file was truncated: drop a partial line and continue decoding from the start
    void reset() { m_decoder.reset(); }

private:
    LogFormat detectFormat(std::string_view sample, Encoding encoding, std::string_view extension) const
    {
        if (m_formatSetting == "text")
            return LogFormat::Text;
        if (m_formatSetting == "dblog")
            return LogFormat::DbLog;
        if (m_formatSetting == "sysinternals")
            return LogFormat::Sysinternals;
        // decode the first two lines of the sample
        std::vector<std::string> lines;
        LineDecoder probe(encoding, m_legacy);
        probe.feed(sample, [&](std::string_view line) {
            if (lines.size() < 2)
                lines.emplace_back(line);
        });
        lines.resize(2);
        return detectLogFormat(lines[0], lines[1], extension);
    }

    void addLine(std::string_view line)
    {
        if (m_format == LogFormat::Text)
        {
            m_sink->add(0, m_origin, line);
            return;
        }
        ParsedLine parsed;
        bool ok = false;
        if (m_format == LogFormat::DbLog)
        {
            if (m_firstLine)
            {
                m_firstLine = false;
                if (isDbLogHeader(line))
                    return;
            }
            ok = parseDbLogLine(line, parsed);
        }
        else
        {
            ok = m_sysinternals.parse(line, parsed);
        }
        if (!ok)
        {
            m_sink->add(0, m_origin, line);
            return;
        }
        auto now = nowSystemNs();
        auto systemNs = parsed.hasSystemTime ? parsed.systemNs : now;
        if (!m_haveFirstSystem)
        {
            m_firstSystemNs = systemNs;
            m_haveFirstSystem = true;
        }
        auto relativeNs = parsed.hasRelativeTime ? static_cast<int64_t>(parsed.relativeSeconds * 1e9) : systemNs - m_firstSystemNs;
        auto origin = parsed.processName.empty() ? m_origin : m_sink->origin(parsed.processName);
        m_sink->add(m_baseSteadyNs + relativeNs, systemNs, parsed.pid, origin, parsed.message);
    }

    Encoding m_configuredEncoding;
    std::shared_ptr<LegacyCodec> m_legacy;
    std::string m_formatSetting;
    LineDecoder m_decoder;
    LogFormat m_format = LogFormat::Text;
    SysinternalsParser m_sysinternals;
    bool m_firstLine = false;

    Sink* m_sink = nullptr;
    uint32_t m_origin = 0;
    int64_t m_baseSteadyNs = 0;
    int64_t m_firstSystemNs = 0;
    bool m_haveFirstSystem = false;
};

} // namespace dv
