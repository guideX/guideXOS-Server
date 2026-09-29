#pragma once

#include "network_settings_contract.h"
#include "settings_inventory_contract.h"

#include <stddef.h>
#include <stdint.h>

namespace gxos {
namespace system_service {

constexpr uint16_t kProtocolVersion = 1;
constexpr uint32_t kWireMagic = 0x42535847u; // "GXSB" in little-endian bytes
constexpr size_t kRequestHeaderBytes = 16;
constexpr size_t kResponseHeaderBytes = 20;
constexpr size_t kSnapshotHeaderBytes = 20;
constexpr size_t kAdapterWireBytes = 140;
constexpr size_t kSnapshotWireBytes = kSnapshotHeaderBytes +
    network_settings::kMaxAdapters * kAdapterWireBytes;
constexpr size_t kConfigurationCandidateWireBytes = 64;
constexpr size_t kConfigurationResultWireBytes = 16;
constexpr size_t kDeviceSnapshotHeaderBytes = 20;
constexpr size_t kDeviceWireBytes = 180;
constexpr size_t kDeviceSnapshotWireBytes = kDeviceSnapshotHeaderBytes +
    settings_inventory::kMaxDevices * kDeviceWireBytes;
constexpr size_t kStorageSnapshotHeaderBytes = 24;
constexpr size_t kDiskWireBytes = 188;
constexpr size_t kVolumeWireBytes = 164;
constexpr size_t kStorageSnapshotWireBytes = kStorageSnapshotHeaderBytes +
    settings_inventory::kMaxDisks * kDiskWireBytes +
    settings_inventory::kMaxVolumes * kVolumeWireBytes;
constexpr size_t kMaxRequestBytes = kRequestHeaderBytes +
    kConfigurationCandidateWireBytes;
constexpr size_t kMaxSnapshotWireBytes = kSnapshotWireBytes > kDeviceSnapshotWireBytes
    ? (kSnapshotWireBytes > kStorageSnapshotWireBytes ? kSnapshotWireBytes : kStorageSnapshotWireBytes)
    : (kDeviceSnapshotWireBytes > kStorageSnapshotWireBytes ? kDeviceSnapshotWireBytes : kStorageSnapshotWireBytes);
constexpr size_t kMaxResponseBytes = kResponseHeaderBytes + kMaxSnapshotWireBytes;
constexpr uint32_t kDefaultRequestTimeoutMs = 200;

enum class RequestType : uint16_t {
    GetNetworkSnapshot = 1,
    SetNetworkConfiguration = 2,
    GetDeviceSnapshot = 3,
    GetStorageSnapshot = 4
};

constexpr uint16_t kNetworkSnapshotResponseType = 0x8001u;
constexpr uint16_t kConfigurationResponseType = 0x8002u;
constexpr uint16_t kDeviceSnapshotResponseType = 0x8003u;
constexpr uint16_t kStorageSnapshotResponseType = 0x8004u;

enum class ResponseStatus : uint16_t {
    Ok = 0,
    Unavailable = 1,
    Unauthorized = 2,
    Unsupported = 3,
    InvalidRequest = 4,
    BadVersion = 5,
    MalformedSnapshot = 6,
    InternalError = 7
};

struct RequestHeader {
    uint16_t version{0};
    uint16_t type{0};
    uint32_t requestId{0};
    uint32_t payloadBytes{0};
};

struct ResponseHeader {
    uint16_t version{0};
    uint16_t type{0};
    uint32_t requestId{0};
    ResponseStatus status{ResponseStatus::InternalError};
    uint16_t payloadBytes{0};
    uint32_t totalBytes{0};
};

struct ConfigurationWireResult {
    network_settings::ConfigurationTransactionResult transaction{};
};

enum class TransportResult : uint8_t {
    Ok = 0,
    Timeout = 1,
    Disconnected = 2,
    Failed = 3
};

enum class ClientResult : uint8_t {
    Ok = 0,
    Unavailable = 1,
    Unauthorized = 2,
    Unsupported = 3,
    InvalidArgument = 4,
    Timeout = 5,
    Disconnected = 6,
    ProtocolError = 7,
    Failed = 8
};

typedef TransportResult (*TransactFunction)(
    void* context,
    const uint8_t* request,
    size_t requestBytes,
    uint8_t* response,
    size_t responseCapacity,
    size_t* responseBytes,
    uint32_t timeoutMs);

struct Transport {
    void* context{nullptr};
    TransactFunction transact{nullptr};
};

inline uint16_t readU16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

inline uint32_t readU32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

inline uint64_t readU64(const uint8_t* bytes)
{
    return static_cast<uint64_t>(readU32(bytes)) |
        (static_cast<uint64_t>(readU32(bytes + 4)) << 32);
}

inline void writeU16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeU32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

inline void writeU64(uint8_t* bytes, uint64_t value)
{
    writeU32(bytes, static_cast<uint32_t>(value));
    writeU32(bytes + 4, static_cast<uint32_t>(value >> 32));
}

inline bool isKnownBackend(network_settings::Backend value)
{
    return value == network_settings::Backend::Unavailable ||
        value == network_settings::Backend::Kernel ||
        value == network_settings::Backend::HostedTest;
}

inline bool isKnownSnapshotState(network_settings::SnapshotState value)
{
    return value == network_settings::SnapshotState::Unavailable ||
        value == network_settings::SnapshotState::NoAdapter ||
        value == network_settings::SnapshotState::AdaptersAvailable;
}

inline bool hasTerminator(const char* text, size_t capacity)
{
    if (!text || capacity == 0) return false;
    for (size_t i = 0; i < capacity; ++i) if (text[i] == '\0') return true;
    return false;
}

inline bool validInterface(const network_settings::NetworkInterfaceInfo& item)
{
    using namespace network_settings;
    if (!hasTerminator(item.stableId, sizeof(item.stableId)) ||
        !hasTerminator(item.name, sizeof(item.name)) ||
        !hasTerminator(item.driver, sizeof(item.driver))) return false;
    if (item.linkState != LinkState::Unknown && item.linkState != LinkState::Down &&
        item.linkState != LinkState::Up) return false;
    if (item.configurationMode != ConfigurationMode::Unknown &&
        item.configurationMode != ConfigurationMode::Dhcp &&
        item.configurationMode != ConfigurationMode::Static) return false;
    if (item.dhcpState < DhcpState::Unknown || item.dhcpState > DhcpState::LeaseRebinding)
        return false;
    if (item.dnsSource < DnsSource::Unknown || item.dnsSource > DnsSource::Unsupported)
        return false;
    const IPv4Value* addresses[] = {
        &item.ipv4Address, &item.subnetMask, &item.gateway, &item.dns
    };
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (!addresses[i]->available && addresses[i]->value != 0) return false;
    }
    return !item.subnetMask.available || isContiguousSubnetMask(item.subnetMask.value);
}

inline bool validSnapshot(const network_settings::NetworkSnapshot& snapshot,
                          bool requireGeneration = true)
{
    using namespace network_settings;
    if (snapshot.version != kContractVersion || !isKnownBackend(snapshot.backend) ||
        !isKnownSnapshotState(snapshot.state) || snapshot.adapterCount > kMaxAdapters ||
        (requireGeneration && snapshot.generation == 0)) return false;
    if (snapshot.state != SnapshotState::Unavailable &&
        snapshot.backend == Backend::Unavailable) return false;
    if (snapshot.state == SnapshotState::Unavailable && snapshot.adapterCount != 0) return false;
    if (snapshot.state == SnapshotState::NoAdapter && snapshot.adapterCount != 0) return false;
    if (snapshot.state == SnapshotState::AdaptersAvailable && snapshot.adapterCount == 0) return false;
    for (uint32_t i = 0; i < snapshot.adapterCount; ++i) {
        if (!validInterface(snapshot.adapters[i])) return false;
    }
    return true;
}

inline bool encodeRequest(uint16_t version, uint16_t type, uint32_t requestId,
                          uint32_t payloadBytes, uint8_t* output, size_t capacity,
                          size_t* outputBytes)
{
    if (outputBytes) *outputBytes = 0;
    if (!output || !outputBytes || capacity < kRequestHeaderBytes) return false;
    writeU32(output, kWireMagic);
    writeU16(output + 4, version);
    writeU16(output + 6, type);
    writeU32(output + 8, requestId);
    writeU32(output + 12, payloadBytes);
    *outputBytes = kRequestHeaderBytes;
    return true;
}

inline bool decodeRequest(const uint8_t* input, size_t inputBytes, RequestHeader* output)
{
    if (!input || !output || inputBytes < kRequestHeaderBytes ||
        inputBytes > kMaxRequestBytes ||
        readU32(input) != kWireMagic) return false;
    output->version = readU16(input + 4);
    output->type = readU16(input + 6);
    output->requestId = readU32(input + 8);
    output->payloadBytes = readU32(input + 12);
    return output->payloadBytes == inputBytes - kRequestHeaderBytes;
}

inline bool encodeConfigurationRequest(
    const network_settings::NetworkConfigurationCandidate& candidate,
    uint32_t requestId, uint8_t* output, size_t capacity, size_t* outputBytes)
{
    using namespace network_settings;
    if (outputBytes) *outputBytes = 0;
    if (!output || !outputBytes || capacity < kMaxRequestBytes ||
        validateCandidate(candidate) != ConfigurationField::None) return false;
    size_t headerBytes = 0;
    if (!encodeRequest(kProtocolVersion,
            static_cast<uint16_t>(RequestType::SetNetworkConfiguration),
            requestId, static_cast<uint32_t>(kConfigurationCandidateWireBytes),
            output, capacity, &headerBytes)) return false;
    uint8_t* payload = output + kRequestHeaderBytes;
    writeU64(payload, candidate.expectedGeneration);
    writeU32(payload + 8, candidate.interfaceId);
    for (size_t i = 0; i < kInterfaceIdBytes; ++i)
        payload[12 + i] = static_cast<uint8_t>(candidate.stableId[i]);
    payload[44] = static_cast<uint8_t>(candidate.mode);
    payload[45] = static_cast<uint8_t>(candidate.dnsMode);
    payload[46] = 0u;
    payload[47] = 0u;
    writeU32(payload + 48, candidate.staticIPv4.address);
    writeU32(payload + 52, candidate.staticIPv4.subnetMask);
    writeU32(payload + 56, candidate.staticIPv4.gateway);
    writeU32(payload + 60, candidate.staticIPv4.dns);
    *outputBytes = kRequestHeaderBytes + kConfigurationCandidateWireBytes;
    return true;
}

inline bool decodeConfigurationRequest(
    const uint8_t* input, size_t inputBytes, RequestHeader* header,
    network_settings::NetworkConfigurationCandidate* output)
{
    using namespace network_settings;
    RequestHeader decoded{};
    if (!output || !decodeRequest(input, inputBytes, &decoded) ||
        decoded.version != kProtocolVersion ||
        decoded.type != static_cast<uint16_t>(RequestType::SetNetworkConfiguration) ||
        decoded.payloadBytes != kConfigurationCandidateWireBytes ||
        inputBytes != kRequestHeaderBytes + kConfigurationCandidateWireBytes)
        return false;
    const uint8_t* payload = input + kRequestHeaderBytes;
    NetworkConfigurationCandidate candidate{};
    candidate.expectedGeneration = readU64(payload);
    candidate.interfaceId = readU32(payload + 8);
    for (size_t i = 0; i < kInterfaceIdBytes; ++i)
        candidate.stableId[i] = static_cast<char>(payload[12 + i]);
    candidate.mode = static_cast<NetworkMode>(payload[44]);
    candidate.dnsMode = static_cast<DnsMode>(payload[45]);
    if (payload[46] != 0u || payload[47] != 0u) return false;
    candidate.staticIPv4.address = readU32(payload + 48);
    candidate.staticIPv4.subnetMask = readU32(payload + 52);
    candidate.staticIPv4.gateway = readU32(payload + 56);
    candidate.staticIPv4.dns = readU32(payload + 60);
    if (validateCandidate(candidate) != ConfigurationField::None) return false;
    if (header) *header = decoded;
    *output = candidate;
    return true;
}

inline bool encodeConfigurationResult(
    const network_settings::ConfigurationTransactionResult& result,
    uint8_t* output, size_t capacity)
{
    if (!output || capacity < kConfigurationResultWireBytes) return false;
    output[0] = static_cast<uint8_t>(result.outcome);
    output[1] = static_cast<uint8_t>(result.field);
    output[2] = result.snapshotRefreshRequired ? 1u : 0u;
    output[3] = 0u;
    writeU64(output + 4, result.resultingGeneration);
    for (size_t i = 12; i < kConfigurationResultWireBytes; ++i) output[i] = 0u;
    return true;
}

inline bool decodeConfigurationResult(
    const uint8_t* input, size_t inputBytes,
    network_settings::ConfigurationTransactionResult* output)
{
    using namespace network_settings;
    if (!input || !output || inputBytes != kConfigurationResultWireBytes ||
        input[0] > static_cast<uint8_t>(TransactionOutcome::CaptureFailed) ||
        input[1] > static_cast<uint8_t>(ConfigurationField::DnsMode) ||
        input[2] > 1u || input[3] != 0u) return false;
    for (size_t i = 12; i < inputBytes; ++i) if (input[i] != 0u) return false;
    output->outcome = static_cast<TransactionOutcome>(input[0]);
    output->field = static_cast<ConfigurationField>(input[1]);
    output->snapshotRefreshRequired = input[2] != 0u;
    output->resultingGeneration = readU64(input + 4);
    return true;
}

inline bool encodeResponseHeader(uint16_t version, uint16_t type, uint32_t requestId,
                                 ResponseStatus status, uint16_t payloadBytes,
                                 uint8_t* output, size_t capacity)
{
    const size_t total = kResponseHeaderBytes + payloadBytes;
    if (!output || capacity < kResponseHeaderBytes || total > kMaxResponseBytes) return false;
    writeU32(output, kWireMagic);
    writeU16(output + 4, version);
    writeU16(output + 6, type);
    writeU32(output + 8, requestId);
    writeU16(output + 12, static_cast<uint16_t>(status));
    writeU16(output + 14, payloadBytes);
    writeU32(output + 16, static_cast<uint32_t>(total));
    return true;
}

inline bool decodeResponseHeader(const uint8_t* input, size_t inputBytes,
                                 ResponseHeader* output)
{
    if (!input || !output || inputBytes < kResponseHeaderBytes ||
        readU32(input) != kWireMagic) return false;
    output->version = readU16(input + 4);
    output->type = readU16(input + 6);
    output->requestId = readU32(input + 8);
    output->status = static_cast<ResponseStatus>(readU16(input + 12));
    output->payloadBytes = readU16(input + 14);
    output->totalBytes = readU32(input + 16);
    if (output->version != kProtocolVersion ||
        (output->type != kNetworkSnapshotResponseType &&
         output->type != kConfigurationResponseType &&
         output->type != kDeviceSnapshotResponseType &&
         output->type != kStorageSnapshotResponseType) ||
        output->totalBytes != kResponseHeaderBytes + output->payloadBytes ||
        output->totalBytes > kMaxResponseBytes || output->totalBytes != inputBytes) return false;
    const uint16_t status = static_cast<uint16_t>(output->status);
    if (status > static_cast<uint16_t>(ResponseStatus::InternalError)) return false;
    if (output->status == ResponseStatus::Ok &&
        ((output->type == kNetworkSnapshotResponseType && output->payloadBytes != kSnapshotWireBytes) ||
         (output->type == kConfigurationResponseType && output->payloadBytes != kConfigurationResultWireBytes) ||
         (output->type == kDeviceSnapshotResponseType && output->payloadBytes != kDeviceSnapshotWireBytes) ||
         (output->type == kStorageSnapshotResponseType && output->payloadBytes != kStorageSnapshotWireBytes)))
        return false;
    if (output->status != ResponseStatus::Ok && output->status != ResponseStatus::Unavailable &&
        output->payloadBytes != 0) return false;
    if (output->status == ResponseStatus::Unavailable && output->payloadBytes != 0) {
        const size_t expected = output->type == kNetworkSnapshotResponseType ? kSnapshotWireBytes :
            output->type == kDeviceSnapshotResponseType ? kDeviceSnapshotWireBytes :
            output->type == kStorageSnapshotResponseType ? kStorageSnapshotWireBytes : 0;
        if (expected == 0 || output->payloadBytes != expected) return false;
    }
    return true;
}

inline bool encodeSnapshot(const network_settings::NetworkSnapshot& snapshot,
                           uint8_t* output, size_t capacity)
{
    using namespace network_settings;
    if (!output || capacity < kSnapshotWireBytes || !validSnapshot(snapshot)) return false;
    writeU32(output, snapshot.version);
    output[4] = static_cast<uint8_t>(snapshot.backend);
    output[5] = static_cast<uint8_t>(snapshot.state);
    output[6] = snapshot.truncated ? 1u : 0u;
    output[7] = 0;
    writeU64(output + 8, snapshot.generation);
    writeU32(output + 16, snapshot.adapterCount);
    for (size_t i = 0; i < kMaxAdapters; ++i) {
        uint8_t* row = output + kSnapshotHeaderBytes + i * kAdapterWireBytes;
        if (i >= snapshot.adapterCount) {
            for (size_t j = 0; j < kAdapterWireBytes; ++j) row[j] = 0;
            continue;
        }
        const NetworkInterfaceInfo& adapter = snapshot.adapters[i];
        writeU32(row, adapter.interfaceId);
        for (size_t j = 0; j < kInterfaceIdBytes; ++j) row[4 + j] = static_cast<uint8_t>(adapter.stableId[j]);
        for (size_t j = 0; j < kNameBytes; ++j) row[36 + j] = static_cast<uint8_t>(adapter.name[j]);
        for (size_t j = 0; j < kDriverBytes; ++j) row[76 + j] = static_cast<uint8_t>(adapter.driver[j]);
        row[116] = static_cast<uint8_t>(adapter.linkState);
        row[117] = static_cast<uint8_t>(adapter.configurationMode);
        row[118] = static_cast<uint8_t>(adapter.dhcpState);
        row[119] = static_cast<uint8_t>(adapter.dnsSource);
        const IPv4Value* addresses[] = {
            &adapter.ipv4Address, &adapter.subnetMask, &adapter.gateway, &adapter.dns
        };
        for (size_t a = 0; a < 4; ++a) {
            writeU32(row + 120 + a * 5, addresses[a]->value);
            row[124 + a * 5] = addresses[a]->available ? 1u : 0u;
        }
    }
    return true;
}

inline bool decodeSnapshot(const uint8_t* input, size_t inputBytes,
                           network_settings::NetworkSnapshot* output)
{
    using namespace network_settings;
    if (!input || !output || inputBytes != kSnapshotWireBytes) return false;
    NetworkSnapshot decoded{};
    decoded.version = readU32(input);
    decoded.backend = static_cast<Backend>(input[4]);
    decoded.state = static_cast<SnapshotState>(input[5]);
    if (input[6] > 1u || input[7] != 0u) return false;
    decoded.truncated = input[6] != 0;
    decoded.generation = readU64(input + 8);
    decoded.adapterCount = readU32(input + 16);
    if (decoded.adapterCount > kMaxAdapters) return false;
    for (size_t i = 0; i < kMaxAdapters; ++i) {
        NetworkInterfaceInfo& adapter = decoded.adapters[i];
        const uint8_t* row = input + kSnapshotHeaderBytes + i * kAdapterWireBytes;
        if (i >= decoded.adapterCount) {
            for (size_t j = 0; j < kAdapterWireBytes; ++j) if (row[j] != 0u) return false;
            continue;
        }
        adapter.interfaceId = readU32(row);
        for (size_t j = 0; j < kInterfaceIdBytes; ++j) adapter.stableId[j] = static_cast<char>(row[4 + j]);
        for (size_t j = 0; j < kNameBytes; ++j) adapter.name[j] = static_cast<char>(row[36 + j]);
        for (size_t j = 0; j < kDriverBytes; ++j) adapter.driver[j] = static_cast<char>(row[76 + j]);
        adapter.linkState = static_cast<LinkState>(row[116]);
        adapter.configurationMode = static_cast<ConfigurationMode>(row[117]);
        adapter.dhcpState = static_cast<DhcpState>(row[118]);
        adapter.dnsSource = static_cast<DnsSource>(row[119]);
        IPv4Value* addresses[] = {
            &adapter.ipv4Address, &adapter.subnetMask, &adapter.gateway, &adapter.dns
        };
        for (size_t a = 0; a < 4; ++a) {
            addresses[a]->value = readU32(row + 120 + a * 5);
            if (row[124 + a * 5] > 1u) return false;
            addresses[a]->available = row[124 + a * 5] != 0;
        }
    }
    if (!validSnapshot(decoded)) return false;
    *output = decoded;
    return true;
}

inline bool encodeDeviceSnapshot(const settings_inventory::DeviceSnapshot& snapshot,
                                 uint8_t* output, size_t capacity)
{
    using namespace settings_inventory;
    if (!output || capacity < kDeviceSnapshotWireBytes || !validDeviceSnapshot(snapshot)) return false;
    writeU32(output, snapshot.version);
    output[4] = static_cast<uint8_t>(snapshot.backend);
    output[5] = static_cast<uint8_t>(snapshot.state);
    output[6] = snapshot.truncated ? 1u : 0u;
    output[7] = 0u;
    writeU64(output + 8, snapshot.generation);
    writeU16(output + 16, snapshot.deviceCount);
    writeU16(output + 18, snapshot.totalDeviceCount);
    for (size_t i = 0; i < kMaxDevices; ++i) {
        uint8_t* row = output + kDeviceSnapshotHeaderBytes + i * kDeviceWireBytes;
        if (i >= snapshot.deviceCount) {
            for (size_t j = 0; j < kDeviceWireBytes; ++j) row[j] = 0u;
            continue;
        }
        const DeviceInfo& item = snapshot.devices[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) row[j] = static_cast<uint8_t>(item.stableId[j]);
        for (size_t j = 0; j < kDeviceNameBytes; ++j) row[40 + j] = static_cast<uint8_t>(item.name[j]);
        for (size_t j = 0; j < kDriverNameBytes; ++j) row[104 + j] = static_cast<uint8_t>(item.driver[j]);
        for (size_t j = 0; j < kDeviceLocationBytes; ++j) row[136 + j] = static_cast<uint8_t>(item.location[j]);
        row[168] = static_cast<uint8_t>(item.category);
        row[169] = static_cast<uint8_t>(item.status);
        writeU16(row + 170, item.vendorId);
        writeU16(row + 172, item.deviceId);
        row[174] = item.bus;
        row[175] = item.slot;
        row[176] = item.function;
        row[177] = item.flags;
        row[178] = row[179] = 0u;
    }
    return true;
}

inline bool decodeDeviceSnapshot(const uint8_t* input, size_t inputBytes,
                                 settings_inventory::DeviceSnapshot* output)
{
    using namespace settings_inventory;
    if (!input || !output || inputBytes != kDeviceSnapshotWireBytes) return false;
    DeviceSnapshot decoded{};
    decoded.version = readU32(input);
    decoded.backend = static_cast<Backend>(input[4]);
    decoded.state = static_cast<SnapshotState>(input[5]);
    if (input[6] > 1u || input[7] != 0u) return false;
    decoded.truncated = input[6] != 0u;
    decoded.generation = readU64(input + 8);
    decoded.deviceCount = readU16(input + 16);
    decoded.totalDeviceCount = readU16(input + 18);
    if (decoded.deviceCount > kMaxDevices) return false;
    for (size_t i = 0; i < kMaxDevices; ++i) {
        const uint8_t* row = input + kDeviceSnapshotHeaderBytes + i * kDeviceWireBytes;
        if (i >= decoded.deviceCount) {
            for (size_t j = 0; j < kDeviceWireBytes; ++j) if (row[j] != 0u) return false;
            continue;
        }
        DeviceInfo& item = decoded.devices[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) item.stableId[j] = static_cast<char>(row[j]);
        for (size_t j = 0; j < kDeviceNameBytes; ++j) item.name[j] = static_cast<char>(row[40 + j]);
        for (size_t j = 0; j < kDriverNameBytes; ++j) item.driver[j] = static_cast<char>(row[104 + j]);
        for (size_t j = 0; j < kDeviceLocationBytes; ++j) item.location[j] = static_cast<char>(row[136 + j]);
        item.category = static_cast<DeviceCategory>(row[168]);
        item.status = static_cast<DeviceStatus>(row[169]);
        item.vendorId = readU16(row + 170);
        item.deviceId = readU16(row + 172);
        item.bus = row[174];
        item.slot = row[175];
        item.function = row[176];
        item.flags = row[177];
        if (row[178] != 0u || row[179] != 0u) return false;
    }
    if (!validDeviceSnapshot(decoded)) return false;
    *output = decoded;
    return true;
}

inline bool encodeStorageSnapshot(const settings_inventory::StorageSnapshot& snapshot,
                                  uint8_t* output, size_t capacity)
{
    using namespace settings_inventory;
    if (!output || capacity < kStorageSnapshotWireBytes || !validStorageSnapshot(snapshot)) return false;
    writeU32(output, snapshot.version);
    output[4] = static_cast<uint8_t>(snapshot.backend);
    output[5] = static_cast<uint8_t>(snapshot.state);
    output[6] = snapshot.truncated ? 1u : 0u;
    output[7] = 0u;
    writeU64(output + 8, snapshot.generation);
    writeU16(output + 16, snapshot.diskCount);
    writeU16(output + 18, snapshot.volumeCount);
    writeU16(output + 20, snapshot.totalDiskCount);
    writeU16(output + 22, snapshot.totalVolumeCount);
    for (size_t i = 0; i < kMaxDisks; ++i) {
        uint8_t* row = output + kStorageSnapshotHeaderBytes + i * kDiskWireBytes;
        if (i >= snapshot.diskCount) {
            for (size_t j = 0; j < kDiskWireBytes; ++j) row[j] = 0u;
            continue;
        }
        const DiskInfo& disk = snapshot.disks[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) row[j] = static_cast<uint8_t>(disk.stableId[j]);
        for (size_t j = 0; j < kDiskNameBytes; ++j) row[40 + j] = static_cast<uint8_t>(disk.name[j]);
        row[88] = static_cast<uint8_t>(disk.transport);
        row[89] = disk.flags;
        writeU32(row + 90, disk.sectorSize);
        writeU64(row + 94, disk.capacityBytes);
        row[102] = static_cast<uint8_t>(disk.partitionTable);
        row[103] = disk.partitionCount;
        row[104] = disk.totalPartitionCount;
        row[105] = row[106] = row[107] = 0u;
        for (size_t p = 0; p < kMaxPartitionsPerDisk; ++p) {
            uint8_t* partRow = row + 108 + p * 20;
            if (p >= disk.partitionCount) {
                for (size_t j = 0; j < 20; ++j) partRow[j] = 0u;
                continue;
            }
            const PartitionInfo& part = disk.partitions[p];
            partRow[0] = part.number;
            partRow[1] = part.typeCode;
            partRow[2] = static_cast<uint8_t>(part.fileSystem);
            partRow[3] = static_cast<uint8_t>(part.mountState);
            writeU64(partRow + 4, part.startSector);
            writeU64(partRow + 12, part.sectorCount);
        }
    }
    const size_t volumesOffset = kStorageSnapshotHeaderBytes + kMaxDisks * kDiskWireBytes;
    for (size_t i = 0; i < kMaxVolumes; ++i) {
        uint8_t* row = output + volumesOffset + i * kVolumeWireBytes;
        if (i >= snapshot.volumeCount) {
            for (size_t j = 0; j < kVolumeWireBytes; ++j) row[j] = 0u;
            continue;
        }
        const VolumeInfo& volume = snapshot.volumes[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) row[j] = static_cast<uint8_t>(volume.stableId[j]);
        for (size_t j = 0; j < kMountPathBytes; ++j) row[40 + j] = static_cast<uint8_t>(volume.mountPath[j]);
        for (size_t j = 0; j < kStableIdBytes; ++j) row[104 + j] = static_cast<uint8_t>(volume.diskStableId[j]);
        row[144] = static_cast<uint8_t>(volume.fileSystem);
        row[145] = volume.flags;
        writeU64(row + 146, volume.capacityBytes);
        writeU64(row + 154, volume.freeBytes);
        row[162] = row[163] = 0u;
    }
    return true;
}

inline bool decodeStorageSnapshot(const uint8_t* input, size_t inputBytes,
                                  settings_inventory::StorageSnapshot* output)
{
    using namespace settings_inventory;
    if (!input || !output || inputBytes != kStorageSnapshotWireBytes) return false;
    StorageSnapshot decoded{};
    decoded.version = readU32(input);
    decoded.backend = static_cast<Backend>(input[4]);
    decoded.state = static_cast<SnapshotState>(input[5]);
    if (input[6] > 1u || input[7] != 0u) return false;
    decoded.truncated = input[6] != 0u;
    decoded.generation = readU64(input + 8);
    decoded.diskCount = readU16(input + 16);
    decoded.volumeCount = readU16(input + 18);
    decoded.totalDiskCount = readU16(input + 20);
    decoded.totalVolumeCount = readU16(input + 22);
    if (decoded.diskCount > kMaxDisks || decoded.volumeCount > kMaxVolumes) return false;
    for (size_t i = 0; i < kMaxDisks; ++i) {
        const uint8_t* row = input + kStorageSnapshotHeaderBytes + i * kDiskWireBytes;
        if (i >= decoded.diskCount) {
            for (size_t j = 0; j < kDiskWireBytes; ++j) if (row[j] != 0u) return false;
            continue;
        }
        DiskInfo& disk = decoded.disks[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) disk.stableId[j] = static_cast<char>(row[j]);
        for (size_t j = 0; j < kDiskNameBytes; ++j) disk.name[j] = static_cast<char>(row[40 + j]);
        disk.transport = static_cast<DiskTransport>(row[88]);
        disk.flags = row[89];
        disk.sectorSize = readU32(row + 90);
        disk.capacityBytes = readU64(row + 94);
        disk.partitionTable = static_cast<PartitionTableState>(row[102]);
        disk.partitionCount = row[103];
        disk.totalPartitionCount = row[104];
        if (row[105] != 0u || row[106] != 0u || row[107] != 0u ||
            disk.partitionCount > kMaxPartitionsPerDisk) return false;
        for (size_t p = 0; p < kMaxPartitionsPerDisk; ++p) {
            const uint8_t* partRow = row + 108 + p * 20;
            if (p >= disk.partitionCount) {
                for (size_t j = 0; j < 20; ++j) if (partRow[j] != 0u) return false;
                continue;
            }
            PartitionInfo& part = disk.partitions[p];
            part.number = partRow[0];
            part.typeCode = partRow[1];
            part.fileSystem = static_cast<FileSystem>(partRow[2]);
            part.mountState = static_cast<MountState>(partRow[3]);
            part.startSector = readU64(partRow + 4);
            part.sectorCount = readU64(partRow + 12);
        }
    }
    const size_t volumesOffset = kStorageSnapshotHeaderBytes + kMaxDisks * kDiskWireBytes;
    for (size_t i = 0; i < kMaxVolumes; ++i) {
        const uint8_t* row = input + volumesOffset + i * kVolumeWireBytes;
        if (i >= decoded.volumeCount) {
            for (size_t j = 0; j < kVolumeWireBytes; ++j) if (row[j] != 0u) return false;
            continue;
        }
        VolumeInfo& volume = decoded.volumes[i];
        for (size_t j = 0; j < kStableIdBytes; ++j) volume.stableId[j] = static_cast<char>(row[j]);
        for (size_t j = 0; j < kMountPathBytes; ++j) volume.mountPath[j] = static_cast<char>(row[40 + j]);
        for (size_t j = 0; j < kStableIdBytes; ++j) volume.diskStableId[j] = static_cast<char>(row[104 + j]);
        volume.fileSystem = static_cast<FileSystem>(row[144]);
        volume.flags = row[145];
        volume.capacityBytes = readU64(row + 146);
        volume.freeBytes = readU64(row + 154);
        if (row[162] != 0u || row[163] != 0u) return false;
    }
    if (!validStorageSnapshot(decoded)) return false;
    *output = decoded;
    return true;
}

} // namespace system_service
} // namespace gxos
