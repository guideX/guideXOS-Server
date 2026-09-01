// Shared, side-effect-free network status interpretation helpers.
//
// These helpers deliberately describe local interface state only.  They do
// not claim Internet reachability unless a caller supplies an explicit,
// successful connectivity probe.
//
// Copyright (c) 2026 guideXOS Server

#ifndef KERNEL_NETWORK_STATUS_H
#define KERNEL_NETWORK_STATUS_H

#include "kernel/types.h"

namespace kernel {
namespace network_status {

enum ConnectionState : uint8_t {
    STATE_NO_ADAPTER = 0,
    STATE_ADAPTER_DETECTED,
    STATE_DRIVER_UNAVAILABLE,
    STATE_DISCONNECTED,
    STATE_IPV4_UNCONFIGURED,
    STATE_ACQUIRING_ADDRESS,
    STATE_IPV4_CONFIGURED,
    STATE_LOCAL_NETWORK_CONFIGURED,
    STATE_ONLINE,
};

struct Inputs {
    bool adapterPresent;
    bool driverBound;
    bool driverReady;
    bool linkUp;
    bool ipv4Configured;
    bool ipv4ConfigurationPending;
    bool gatewayConfigured;
    bool dnsConfigured;
    bool connectivityVerified;
};

inline ConnectionState classify(const Inputs& input)
{
    if (!input.adapterPresent) return STATE_NO_ADAPTER;
    if (!input.driverBound) return STATE_DRIVER_UNAVAILABLE;
    if (!input.driverReady) return STATE_ADAPTER_DETECTED;
    if (!input.linkUp) return STATE_DISCONNECTED;
    if (!input.ipv4Configured) {
        return input.ipv4ConfigurationPending
            ? STATE_ACQUIRING_ADDRESS
            : STATE_IPV4_UNCONFIGURED;
    }
    if (input.connectivityVerified) return STATE_ONLINE;
    if (input.gatewayConfigured) return STATE_LOCAL_NETWORK_CONFIGURED;
    return STATE_IPV4_CONFIGURED;
}

inline const char* state_to_string(ConnectionState state)
{
    switch (state) {
        case STATE_NO_ADAPTER:              return "No adapter";
        case STATE_ADAPTER_DETECTED:        return "Adapter detected";
        case STATE_DRIVER_UNAVAILABLE:      return "Driver unavailable";
        case STATE_DISCONNECTED:            return "Disconnected";
        case STATE_IPV4_UNCONFIGURED:       return "IPv4 unconfigured";
        case STATE_ACQUIRING_ADDRESS:       return "Acquiring address";
        case STATE_IPV4_CONFIGURED:         return "IPv4 configured";
        case STATE_LOCAL_NETWORK_CONFIGURED:return "Local network configured";
        case STATE_ONLINE:                  return "Online (verified)";
        default:                            return "Unknown";
    }
}

inline bool is_supported_intel_e1000(uint16_t vendorId, uint16_t deviceId)
{
    return vendorId == 0x8086 &&
           (deviceId == 0x100E || deviceId == 0x10D3 || deviceId == 0x153A ||
            deviceId == 0x156F);
}

inline const char* driver_name(uint16_t vendorId, uint16_t deviceId)
{
    if (vendorId == 0x8086 && deviceId == 0x100E) return "intel-e1000 (82540EM)";
    if (vendorId == 0x8086 && deviceId == 0x10D3) return "intel-e1000e (82574L)";
    if (vendorId == 0x8086 && deviceId == 0x153A) return "intel-e1000 (I217-LM)";
    if (vendorId == 0x8086 && deviceId == 0x156F) return "intel-i219-lm (PCH)";
    return "unsupported";
}

} // namespace network_status
} // namespace kernel

#endif // KERNEL_NETWORK_STATUS_H
