#pragma once

#include "system_service_protocol.h"

namespace gxos {
namespace system_service {

typedef ResponseStatus (*ReadDeviceSnapshotFunction)(
    void* context, settings_inventory::DeviceSnapshot* output);
typedef ResponseStatus (*ReadStorageSnapshotFunction)(
    void* context, settings_inventory::StorageSnapshot* output);

struct DispatchProviders {
    network_settings::Provider network{};
    void* inventoryContext{nullptr};
    ReadDeviceSnapshotFunction readDevices{nullptr};
    ReadStorageSnapshotFunction readStorage{nullptr};
};

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
                     const DispatchProviders& provider,
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
    settings_inventory::DeviceSnapshot devices{};
    settings_inventory::StorageSnapshot storage{};
    enum class Payload { None, Network, Devices, Storage } payload = Payload::None;
    uint16_t responseType = kNetworkSnapshotResponseType;
    if (request.version != kProtocolVersion) {
        status = ResponseStatus::BadVersion;
    } else if (request.type == static_cast<uint16_t>(RequestType::SetNetworkConfiguration)) {
        responseType = kConfigurationResponseType;
        network_settings::NetworkConfigurationCandidate candidate{};
        RequestHeader decoded{};
        if (!decodeConfigurationRequest(requestBytes, requestSize, &decoded, &candidate)) {
            status = ResponseStatus::InvalidRequest;
        } else if (trust != DispatchTrust::TrustedSystemServicePeer) {
            status = ResponseStatus::Unauthorized;
        } else {
            // COM2's TCP peer is not authenticated. The trusted marker permits
            // read-only service access only; it never grants mutation authority.
            status = ResponseStatus::Unauthorized;
        }
    } else if (request.payloadBytes != 0u) {
        status = ResponseStatus::InvalidRequest;
    } else if (trust != DispatchTrust::TrustedSystemServicePeer) {
        status = ResponseStatus::Unauthorized;
    } else if (request.type == static_cast<uint16_t>(RequestType::GetNetworkSnapshot)) {
        responseType = kNetworkSnapshotResponseType;
        if (!provider.network.readSnapshot) {
            status = ResponseStatus::Unavailable;
        } else {
            const network_settings::Result result = provider.network.readSnapshot(
                provider.network.context, &snapshot);
            status = responseStatusFor(result);
            if (result == network_settings::Result::Ok ||
                result == network_settings::Result::Unavailable) {
                if (!validSnapshot(snapshot)) status = ResponseStatus::MalformedSnapshot;
                else { includeSnapshot = true; payload = Payload::Network; }
            }
        }
    } else if (request.type == static_cast<uint16_t>(RequestType::GetDeviceSnapshot)) {
        responseType = kDeviceSnapshotResponseType;
        if (!provider.readDevices) {
            status = ResponseStatus::Unavailable;
        } else {
            status = provider.readDevices(provider.inventoryContext, &devices);
            if (status == ResponseStatus::Ok || status == ResponseStatus::Unavailable) {
                if (!settings_inventory::validDeviceSnapshot(devices)) status = ResponseStatus::MalformedSnapshot;
                else { includeSnapshot = true; payload = Payload::Devices; }
            }
        }
    } else if (request.type == static_cast<uint16_t>(RequestType::GetStorageSnapshot)) {
        responseType = kStorageSnapshotResponseType;
        if (!provider.readStorage) {
            status = ResponseStatus::Unavailable;
        } else {
            status = provider.readStorage(provider.inventoryContext, &storage);
            if (status == ResponseStatus::Ok || status == ResponseStatus::Unavailable) {
                if (!settings_inventory::validStorageSnapshot(storage)) status = ResponseStatus::MalformedSnapshot;
                else { includeSnapshot = true; payload = Payload::Storage; }
            }
        }
    } else {
        status = ResponseStatus::Unsupported;
    }

    const uint16_t payloadBytes = !includeSnapshot ? 0u :
        payload == Payload::Network ? static_cast<uint16_t>(kSnapshotWireBytes) :
        payload == Payload::Devices ? static_cast<uint16_t>(kDeviceSnapshotWireBytes) :
        static_cast<uint16_t>(kStorageSnapshotWireBytes);
    if (!encodeResponseHeader(kProtocolVersion, responseType, request.requestId,
            status, payloadBytes, responseBytes, responseCapacity)) return false;
    if (includeSnapshot) {
        uint8_t* body = responseBytes + kResponseHeaderBytes;
        const size_t bodyCapacity = responseCapacity - kResponseHeaderBytes;
        const bool encoded = payload == Payload::Network
            ? encodeSnapshot(snapshot, body, bodyCapacity)
            : payload == Payload::Devices
            ? encodeDeviceSnapshot(devices, body, bodyCapacity)
            : encodeStorageSnapshot(storage, body, bodyCapacity);
        if (!encoded) return false;
    }
    *responseSize = kResponseHeaderBytes + payloadBytes;
    return true;
}

inline bool dispatch(const uint8_t* requestBytes, size_t requestSize,
                     const network_settings::Provider& provider,
                     DispatchTrust trust,
                     uint8_t* responseBytes, size_t responseCapacity,
                     size_t* responseSize)
{
    DispatchProviders composite{};
    composite.network = provider;
    return dispatch(requestBytes, requestSize, composite, trust,
        responseBytes, responseCapacity, responseSize);
}

} // namespace system_service
} // namespace gxos
