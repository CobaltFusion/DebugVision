#pragma once

// Factories of the built-in source types, used by the SourceRegistry

#include "dvcore/Source.h"
#include "dvcore/SourceConfig.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace dv {

#ifdef _WIN32
std::unique_ptr<Source> createOutputDebugStringSource(const SourceConfig& config);
#endif
std::unique_ptr<Source> createUdpSource(const SourceConfig& config);
std::unique_ptr<Source> createTcpListenSource(const SourceConfig& config);
std::unique_ptr<Source> createTcpConnectSource(const SourceConfig& config);
std::unique_ptr<Source> createFileSource(const SourceConfig& config);
std::unique_ptr<Source> createProcessSource(const SourceConfig& config);
std::unique_ptr<Source> createStdinSource(const SourceConfig& config);
std::unique_ptr<Source> createDbgviewAgentSource(const SourceConfig& config);
std::unique_ptr<Source> createHttpSource(const SourceConfig& config);

// a parsed Content-Range header ("bytes 0-99/1234" or "bytes */1234"); -1 for the parts that are missing or "*"
struct ContentRange
{
    int64_t first = -1;
    int64_t last = -1;
    int64_t total = -1;
};
ContentRange parseContentRange(std::string_view header);

// "[UDP {ip}:{port}]" style process name templates
std::string expandProcessName(const std::string& pattern, const std::string& ip, uint16_t port);

// creates the legacy codec for a source; console children use the OEM code page on Windows
std::shared_ptr<LegacyCodec> legacyCodecFor(const SourceConfig& config, bool consoleOutput = false);

} // namespace dv
