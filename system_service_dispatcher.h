#pragma once

#include "system_service_protocol.h"

namespace gxos {
namespace system_service {

// Authorization is supplied by the runtime endpoint after it verifies its
// caller or transport. It is never read from a request field.
enum class DispatchTrust : uint8_t {
    Untrusted = 0,
    TrustedSystemServicePeer = 1
};

inline ResponseStatus responseStatusFor(network_settings::Result result)
{
    switch (result) {
    case network_settings::Result::Ok: return ResponseStatus::Ok;
    case network_settings::Result::Unavailable: return ResponseStatus::Unavailable;
    case network_settings::Result::Unauthorized: return ResponseStatus::Unauthorized;
    case network_settings::Result::Unsupported: return ResponseStatus::Unsupported;
    case network_settings::Result::InvalidArgument: return ResponseStatus::InvalidRequest;
    case network_settings::Result::StaleGeneration: return ResponseStatus::InvalidRequest;
    default: return ResponseStatus::InternalError;
    }
}

inline bool dispatch(const uint8_t* requestBytes, size_t requestSize,
                     const network_settings::Provider& provider,
                     DispatchTrust trust,
                     uint8_t* responseBytes, size_t responseCapacity,
                     size_t* responseSize)
{
    if (responseSize) *responseSize = 0;
    if (!responseBytes || !responseSize || responseCapacity < kResponseHeaderBytes)
        return false;

    RequestHeader request{};
    if (!decodeRequest(requestBytes, requestSize, &request)) {
        return encodeResponseHeader(kProtocolVersion, 0x8001u, 0,
            ResponseStatus::InvalidRequest, 0, responseBytes, responseCapacity) &&
            ((*responseSize = kResponseHeaderBytes), true);
    }

    ResponseStatus status = ResponseStatus::Ok;
    network_settings::NetworkSnapshot snapshot{};
    bool includeSnapshot = false;
    if (request.version != kProtocolVersion) {
        status = ResponseStatus::BadVersion;
    } else if (request.payloadBytes != 0u) {
        status = ResponseStatus::InvalidRequest;
    } else if (trust != DispatchTrust::TrustedSystemServicePeer) {
        status = ResponseStatus::Unauthorized;
    } else if (request.type != static_cast<uint16_t>(RequestType::GetNetworkSnapshot)) {
        status = ResponseStatus::Unsupported;
    } else if (!provider.readSnapshot) {
        status = ResponseStatus::Unavailable;
    } else {
        const network_settings::Result result = provider.readSnapshot(provider.context, &snapshot);
        status = responseStatusFor(result);
        if (result == network_settings::Result::Ok ||
            result == network_settings::Result::Unavailable) {
            if (!validSnapshot(snapshot)) status = ResponseStatus::MalformedSnapshot;
            else includeSnapshot = true;
        }
    }

    const uint16_t payloadBytes = includeSnapshot
        ? static_cast<uint16_t>(kSnapshotWireBytes) : 0u;
    if (!encodeResponseHeader(kProtocolVersion, 0x8001u, request.requestId,
            status, payloadBytes, responseBytes, responseCapacity)) return false;
    if (includeSnapshot && !encodeSnapshot(snapshot,
            responseBytes + kResponseHeaderBytes,
            responseCapacity - kResponseHeaderBytes)) return false;
    *responseSize = kResponseHeaderBytes + payloadBytes;
    return true;
}

} // namespace system_service
} // namespace gxos
