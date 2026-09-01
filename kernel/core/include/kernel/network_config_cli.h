// Bounded parser helpers for the netconfig shell command.

#ifndef KERNEL_NETWORK_CONFIG_CLI_H
#define KERNEL_NETWORK_CONFIG_CLI_H

#include "kernel/ipv4.h"

namespace kernel {
namespace network_config_cli {

enum Operation : uint8_t {
    OP_INVALID = 0,
    OP_SHOW,
    OP_STATIC,
    OP_DHCP,
};

inline bool equals(const char* left, const char* right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (*left != *right) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

inline Operation operation_from_string(const char* value)
{
    if (equals(value, "show")) return OP_SHOW;
    if (equals(value, "static")) return OP_STATIC;
    if (equals(value, "dhcp") || equals(value, "automatic")) return OP_DHCP;
    return OP_INVALID;
}

inline bool parse_mask(const char* value, uint32_t* mask)
{
    if (!value || !mask || value[0] == '\0') return false;

    bool hasDot = false;
    for (uint32_t i = 0; value[i] != '\0'; ++i) {
        if (value[i] == '.') {
            hasDot = true;
            break;
        }
    }

    if (!hasDot) {
        uint32_t prefix = 0;
        for (uint32_t i = 0; value[i] != '\0'; ++i) {
            if (value[i] < '0' || value[i] > '9') return false;
            prefix = prefix * 10u + static_cast<uint32_t>(value[i] - '0');
            if (prefix > 32u) return false;
        }
        return ipv4::mask_from_prefix(static_cast<uint8_t>(prefix), mask) &&
               ipv4::is_valid_subnet_mask(*mask);
    }

    return ipv4::ip_from_string(value, mask) && ipv4::is_valid_subnet_mask(*mask);
}

} // namespace network_config_cli
} // namespace kernel

#endif // KERNEL_NETWORK_CONFIG_CLI_H
