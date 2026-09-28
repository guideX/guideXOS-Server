#pragma once

#include "network_settings_contract.h"

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
constexpr size_t kMaxRequestBytes = kRequestHeaderBytes;
constexpr size_t kMaxResponseBytes = kResponseHeaderBytes + kSnapshotWireBytes;
constexpr uint32_t kDefaultRequestTimeoutMs = 200;

enum class RequestType : uint16_t {
    GetNetworkSnapshot = 1
};

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
    if (!input || !output || inputBytes != kRequestHeaderBytes ||
        readU32(input) != kWireMagic) return false;
    output->version = readU16(input + 4);
    output->type = readU16(input + 6);
    output->requestId = readU32(input + 8);
    output->payloadBytes = readU32(input + 12);
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
    if (output->version != kProtocolVersion || output->type != 0x8001u ||
        output->totalBytes != kResponseHeaderBytes + output->payloadBytes ||
        output->totalBytes > kMaxResponseBytes || output->totalBytes != inputBytes) return false;
    const uint16_t status = static_cast<uint16_t>(output->status);
    if (status > static_cast<uint16_t>(ResponseStatus::InternalError)) return false;
    if (output->status == ResponseStatus::Ok && output->payloadBytes != kSnapshotWireBytes)
        return false;
    if (output->status != ResponseStatus::Ok && output->status != ResponseStatus::Unavailable &&
        output->payloadBytes != 0) return false;
    if (output->status == ResponseStatus::Unavailable && output->payloadBytes != 0 &&
        output->payloadBytes != kSnapshotWireBytes) return false;
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

} // namespace system_service
} // namespace gxos
