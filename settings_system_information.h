#pragma once

#include <cstdint>
#include <string>

namespace gxos {
namespace apps {
namespace settings {

struct HostedSystemInformation {
    std::string runtime;
    std::string computerName;
    std::string processorName;
    std::string architecture;
    std::string firmware;
    uint32_t logicalProcessorCount{0};
    uint64_t installedMemoryBytes{0};
};

HostedSystemInformation readHostedSystemInformation();

} // namespace settings
} // namespace apps
} // namespace gxos
