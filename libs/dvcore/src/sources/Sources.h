#pragma once

// Factories of the built-in source types, used by the SourceRegistry

#include "dvcore/Source.h"
#include "dvcore/SourceConfig.h"

#include <memory>

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

// "[UDP {ip}:{port}]" style process name templates
std::string expandProcessName(const std::string& pattern, const std::string& ip, uint16_t port);

// creates the legacy codec for a source; console children use the OEM code page on Windows
std::shared_ptr<LegacyCodec> legacyCodecFor(const SourceConfig& config, bool consoleOutput = false);

} // namespace dv
