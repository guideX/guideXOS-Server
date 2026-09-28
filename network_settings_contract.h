#pragma once

// Bounded, pointer-free contract shared by Settings and the kernel provider.
// The contract deliberately contains copies of state only; callers never see
// NIC, DHCP, or IPv4 implementation pointers.

#include <stddef.h>
#include <stdint.h>

namespace gxos {
namespace network_settings {

constexpr uint32_t kContractVersion = 1;
constexpr size_t kMaxAdapters = 4;
constexpr size_t kInterfaceIdBytes = 32;
constexpr size_t kNameBytes = 40;
constexpr size_t kDriverBytes = 40;
constexpr uint64_t kRefreshIntervalMs = 2000;

class RefreshPolicy {
public:
    void setActive(bool active, uint64_t nowMs)
    {
        if (active == m_active) return;
        m_active = active;
        m_dueMs = active ? nowMs : 0;
    }

    bool due(uint64_t nowMs) const { return m_active && nowMs >= m_dueMs; }

    void completed(uint64_t nowMs)
    {
        if (m_active) m_dueMs = nowMs + kRefreshIntervalMs;
    }

    bool active() const { return m_active; }
    uint64_t dueAtMs() const { return m_dueMs; }

private:
    bool m_active{false};
    uint64_t m_dueMs{0};
};

enum class Backend : uint8_t {
    Unavailable = 0,
    Kernel = 1,
    HostedTest = 2
};

enum class SnapshotState : uint8_t {
    Unavailable = 0,
    NoAdapter = 1,
    AdaptersAvailable = 2
};

enum class LinkState : uint8_t {
    Unknown = 0,
    Down = 1,
    Up = 2
};

enum class ConfigurationMode : uint8_t {
    Unknown = 0,
    Dhcp = 1,
    Static = 2
};

enum class DhcpState : uint8_t {
    Unknown = 0,
    NotRunning = 1,
    Initializing = 2,
    LeaseAcquired = 3,
    FailedNoLease = 4,
    LeaseRenewing = 5,
    LeaseRebinding = 6
};

enum class DnsSource : uint8_t {
    Unknown = 0,
    Dhcp = 1,
    Static = 2,
    Unsupported = 3
};

struct IPv4Value {
    uint32_t value{0}; // host order; meaningful only when available is true
    bool available{false};
};

struct NetworkInterfaceInfo {
    uint32_t interfaceId{0};
    char stableId[kInterfaceIdBytes]{};
    char name[kNameBytes]{};
    char driver[kDriverBytes]{};
    LinkState linkState{LinkState::Unknown};
    ConfigurationMode configurationMode{ConfigurationMode::Unknown};
    DhcpState dhcpState{DhcpState::Unknown};
    DnsSource dnsSource{DnsSource::Unknown};
    IPv4Value ipv4Address{};
    IPv4Value subnetMask{};
    IPv4Value gateway{};
    IPv4Value dns{};
};

struct NetworkSnapshot {
    uint32_t version{kContractVersion};
    Backend backend{Backend::Unavailable};
    SnapshotState state{SnapshotState::Unavailable};
    uint64_t generation{0};
    uint32_t adapterCount{0};
    bool truncated{false};
    NetworkInterfaceInfo adapters[kMaxAdapters]{};
};

enum class Client : uint8_t {
    OrdinaryApplication = 0,
    BuiltInSettings = 1
};

enum class Operation : uint8_t {
    ReadStatus = 0,
    ChangeConfiguration = 1
};

enum class Result : uint8_t {
    Ok = 0,
    Unauthorized = 1,
    Unavailable = 2,
    Unsupported = 3,
    InvalidArgument = 4,
    StaleGeneration = 5
};

struct Provider {
    void* context{nullptr};
    Result (*readSnapshot)(void* context, NetworkSnapshot* output){nullptr};
};

// This surface is currently a Settings-only OS service. It is not in the
// NativeHostCallTable or the public app SDK, so a manifest cannot grant an
// ordinary app access by self-declaring a permission. The local kernel has a
// static transaction foundation, but COM2 does not authenticate its peer, so
// remote configuration remains Unsupported at this service boundary.
inline bool isAuthorized(Client client, Operation operation)
{
    if (client != Client::BuiltInSettings) return false;
    return operation == Operation::ReadStatus || operation == Operation::ChangeConfiguration;
}

inline Result readSnapshot(const Provider& provider, Client client, NetworkSnapshot* output)
{
    if (!output) return Result::InvalidArgument;
    *output = NetworkSnapshot{};
    if (!isAuthorized(client, Operation::ReadStatus)) return Result::Unauthorized;
    if (!provider.readSnapshot) return Result::Unavailable;

    const Result result = provider.readSnapshot(provider.context, output);
    if (output->adapterCount > kMaxAdapters) {
        output->adapterCount = static_cast<uint32_t>(kMaxAdapters);
        output->truncated = true;
    }
    if (output->state == SnapshotState::AdaptersAvailable && output->adapterCount == 0)
        output->state = SnapshotState::NoAdapter;
    if (output->state == SnapshotState::NoAdapter) output->adapterCount = 0;
    return result;
}

inline Result requestConfiguration(Client client)
{
    if (!isAuthorized(client, Operation::ChangeConfiguration)) return Result::Unauthorized;
    // No provider currently supports safe atomic DHCP/static updates.
    return Result::Unsupported;
}

inline bool copyText(char* destination, size_t capacity, const char* source)
{
    if (!destination || capacity == 0 || !source) return false;
    size_t i = 0;
    while (i + 1 < capacity && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
    return source[i] == '\0';
}

inline bool parseIPv4(const char* text, uint32_t* output)
{
    if (!text || !output || !text[0]) return false;
    uint32_t address = 0;
    uint32_t octet = 0;
    uint32_t digits = 0;
    uint32_t parts = 0;
    for (const char* p = text; ; ++p) {
        const char c = *p;
        if (c >= '0' && c <= '9') {
            if (++digits > 3) return false;
            octet = octet * 10u + static_cast<uint32_t>(c - '0');
            if (octet > 255u) return false;
            continue;
        }
        if (c != '.' && c != '\0') return false;
        if (digits == 0 || parts >= 4) return false;
        address = (address << 8) | octet;
        ++parts;
        if (c == '\0') break;
        octet = 0;
        digits = 0;
    }
    if (parts != 4) return false;
    *output = address;
    return true;
}

inline bool isContiguousSubnetMask(uint32_t mask)
{
    bool sawZero = false;
    for (int bit = 31; bit >= 0; --bit) {
        const bool set = (mask & (1u << bit)) != 0;
        if (sawZero && set) return false;
        if (!set) sawZero = true;
    }
    return true;
}

struct IPv4Configuration {
    uint32_t address{0};
    uint32_t subnetMask{0};
    uint32_t gateway{0};
    uint32_t dns{0};
};

enum class NetworkMode : uint8_t {
    Dhcp = 1,
    Static = 2
};

enum class DnsMode : uint8_t {
    Automatic = 1,
    Manual = 2
};

// Complete, fixed-size request bound to the interface identity and snapshot
// generation the user edited. Addresses use host byte order. The stable ID is
// copied from NetworkInterfaceInfo and is never a pointer or caller claim.
struct NetworkConfigurationCandidate {
    uint64_t expectedGeneration{0};
    uint32_t interfaceId{0};
    char stableId[kInterfaceIdBytes]{};
    NetworkMode mode{NetworkMode::Dhcp};
    DnsMode dnsMode{DnsMode::Automatic};
    IPv4Configuration staticIPv4{};
};

enum class ConfigurationField : uint8_t {
    None = 0,
    Generation = 1,
    Interface = 2,
    Mode = 3,
    Address = 4,
    SubnetMask = 5,
    Gateway = 6,
    Dns = 7,
    DnsMode = 8
};

enum class TransactionOutcome : uint8_t {
    Success = 0,
    ValidationFailed = 1,
    StaleInterface = 2,
    MissingInterface = 3,
    ApplyFailedRolledBack = 4,
    ApplyFailedRollbackFailed = 5,
    VerifyFailedRolledBack = 6,
    VerifyFailedRollbackFailed = 7,
    Unsupported = 8,
    CaptureFailed = 9
};

struct ConfigurationTransactionResult {
    TransactionOutcome outcome{TransactionOutcome::Unsupported};
    ConfigurationField field{ConfigurationField::None};
    uint64_t resultingGeneration{0};
    bool snapshotRefreshRequired{true};
};

inline bool validUnicastIPv4(uint32_t address)
{
    if (address == 0u || address == 0xFFFFFFFFu) return false;
    const uint32_t firstOctet = (address >> 24) & 0xFFu;
    return firstOctet != 0u && firstOctet != 127u && firstOctet < 224u;
}

inline bool hasTextTerminator(const char* text, size_t capacity)
{
    if (!text || capacity == 0u) return false;
    for (size_t i = 0; i < capacity; ++i) if (text[i] == '\0') return true;
    return false;
}

inline ConfigurationField validateCandidate(
    const NetworkConfigurationCandidate& candidate)
{
    if (candidate.expectedGeneration == 0u) return ConfigurationField::Generation;
    if (!hasTextTerminator(candidate.stableId,
            sizeof(candidate.stableId)) || candidate.stableId[0] == '\0')
        return ConfigurationField::Interface;
    if (candidate.mode != NetworkMode::Dhcp && candidate.mode != NetworkMode::Static)
        return ConfigurationField::Mode;
    if (candidate.dnsMode != DnsMode::Automatic && candidate.dnsMode != DnsMode::Manual)
        return ConfigurationField::DnsMode;

    const IPv4Configuration& config = candidate.staticIPv4;
    if (candidate.mode == NetworkMode::Dhcp) {
        if (candidate.dnsMode != DnsMode::Automatic)
            return ConfigurationField::DnsMode;
        if (config.address != 0u || config.subnetMask != 0u ||
            config.gateway != 0u || config.dns != 0u)
            return ConfigurationField::Address;
        return ConfigurationField::None;
    }

    if (!validUnicastIPv4(config.address)) return ConfigurationField::Address;
    if (!isContiguousSubnetMask(config.subnetMask)) return ConfigurationField::SubnetMask;
    // The current IPv4 router has no point-to-point route semantics. Require a
    // conventional /1 through /30 network with a usable host address.
    uint32_t prefix = 0;
    for (int bit = 31; bit >= 0; --bit) {
        if ((config.subnetMask & (1u << bit)) == 0u) break;
        ++prefix;
    }
    if (prefix < 1u || prefix > 30u) return ConfigurationField::SubnetMask;
    const uint32_t network = config.address & config.subnetMask;
    const uint32_t broadcast = network | ~config.subnetMask;
    if (config.address == network || config.address == broadcast)
        return ConfigurationField::Address;
    if (config.gateway != 0u && (!validUnicastIPv4(config.gateway) ||
        config.gateway == network || config.gateway == broadcast ||
        config.gateway == config.address ||
        (config.gateway & config.subnetMask) != network))
        return ConfigurationField::Gateway;
    if (candidate.dnsMode == DnsMode::Automatic) {
        if (config.dns != 0u) return ConfigurationField::Dns;
        return ConfigurationField::None;
    }
    if (!validUnicastIPv4(config.dns)) return ConfigurationField::Dns;
    return ConfigurationField::None;
}

inline bool sameInterfaceIdentity(const char* left, const char* right,
                                  size_t capacity)
{
    if (!left || !right || capacity == 0u) return false;
    for (size_t i = 0; i < capacity; ++i) {
        if (left[i] != right[i]) return false;
        if (left[i] == '\0') return true;
    }
    return false;
}

inline TransactionOutcome validateCandidateInterface(
    const NetworkSnapshot& snapshot,
    const NetworkConfigurationCandidate& candidate)
{
    if (snapshot.generation != candidate.expectedGeneration)
        return TransactionOutcome::StaleInterface;
    if (snapshot.state != SnapshotState::AdaptersAvailable ||
        snapshot.adapterCount == 0u) return TransactionOutcome::MissingInterface;
    for (uint32_t i = 0; i < snapshot.adapterCount && i < kMaxAdapters; ++i) {
        const NetworkInterfaceInfo& adapter = snapshot.adapters[i];
        if (adapter.interfaceId != candidate.interfaceId) continue;
        return sameInterfaceIdentity(adapter.stableId, candidate.stableId,
            kInterfaceIdBytes) ? TransactionOutcome::Success :
            TransactionOutcome::MissingInterface;
    }
    return TransactionOutcome::MissingInterface;
}

struct IPv4ConfigurationInput {
    char address[16]{};
    char subnetMask[16]{};
    char gateway[16]{};
    char dns[16]{};
};

enum class ValidationField : uint8_t {
    None = 0,
    Address = 1,
    SubnetMask = 2,
    Gateway = 3,
    Dns = 4
};

inline ValidationField validateStaticIPv4(const IPv4ConfigurationInput& input,
                                          IPv4Configuration* output)
{
    if (!output) return ValidationField::Address;
    IPv4Configuration candidate{};
    if (!parseIPv4(input.address, &candidate.address) || candidate.address == 0 ||
        (candidate.address & 0xF0000000u) == 0xE0000000u)
        return ValidationField::Address;
    if (!parseIPv4(input.subnetMask, &candidate.subnetMask) ||
        !isContiguousSubnetMask(candidate.subnetMask))
        return ValidationField::SubnetMask;
    if (!parseIPv4(input.gateway, &candidate.gateway)) return ValidationField::Gateway;
    if (candidate.gateway != 0 &&
        (candidate.gateway & candidate.subnetMask) != (candidate.address & candidate.subnetMask))
        return ValidationField::Gateway;
    if (!parseIPv4(input.dns, &candidate.dns)) return ValidationField::Dns;
    *output = candidate;
    return ValidationField::None;
}

inline bool formatIPv4(uint32_t address, char output[16])
{
    if (!output) return false;
    uint32_t octets[4] = {
        (address >> 24) & 0xFFu,
        (address >> 16) & 0xFFu,
        (address >> 8) & 0xFFu,
        address & 0xFFu
    };
    size_t at = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (i) output[at++] = '.';
        const uint32_t value = octets[i];
        if (value >= 100) output[at++] = static_cast<char>('0' + value / 100);
        if (value >= 10) output[at++] = static_cast<char>('0' + (value / 10) % 10);
        output[at++] = static_cast<char>('0' + value % 10);
    }
    output[at] = '\0';
    return true;
}

inline bool adapterForGeneration(const NetworkSnapshot& snapshot, uint32_t interfaceId,
                                 uint64_t generation, NetworkInterfaceInfo* output)
{
    if (!output || generation != snapshot.generation) return false;
    for (uint32_t i = 0; i < snapshot.adapterCount && i < kMaxAdapters; ++i) {
        if (snapshot.adapters[i].interfaceId == interfaceId) {
            *output = snapshot.adapters[i];
            return true;
        }
    }
    return false;
}

inline bool sameSnapshot(const NetworkSnapshot& left, const NetworkSnapshot& right)
{
    if (left.version != right.version || left.backend != right.backend || left.state != right.state ||
        left.generation != right.generation || left.adapterCount != right.adapterCount ||
        left.truncated != right.truncated) return false;
    const uint32_t count = left.adapterCount > kMaxAdapters ? static_cast<uint32_t>(kMaxAdapters) : left.adapterCount;
    for (uint32_t i = 0; i < count; ++i) {
        const NetworkInterfaceInfo& a = left.adapters[i];
        const NetworkInterfaceInfo& b = right.adapters[i];
        if (a.interfaceId != b.interfaceId || a.linkState != b.linkState ||
            a.configurationMode != b.configurationMode || a.dhcpState != b.dhcpState ||
            a.dnsSource != b.dnsSource || a.ipv4Address.value != b.ipv4Address.value ||
            a.ipv4Address.available != b.ipv4Address.available ||
            a.subnetMask.value != b.subnetMask.value || a.subnetMask.available != b.subnetMask.available ||
            a.gateway.value != b.gateway.value || a.gateway.available != b.gateway.available ||
            a.dns.value != b.dns.value || a.dns.available != b.dns.available) return false;
        for (size_t j = 0; j < kInterfaceIdBytes; ++j) if (a.stableId[j] != b.stableId[j]) return false;
        for (size_t j = 0; j < kNameBytes; ++j) if (a.name[j] != b.name[j]) return false;
        for (size_t j = 0; j < kDriverBytes; ++j) if (a.driver[j] != b.driver[j]) return false;
    }
    return true;
}

inline const char* dhcpStateText(DhcpState state)
{
    switch (state) {
    case DhcpState::NotRunning: return "Not running";
    case DhcpState::Initializing: return "Obtaining address...";
    case DhcpState::LeaseAcquired: return "Lease acquired";
    case DhcpState::FailedNoLease: return "Failed; no lease";
    case DhcpState::LeaseRenewing: return "Renewing lease...";
    case DhcpState::LeaseRebinding: return "Rebinding lease...";
    case DhcpState::Unknown: default: return "Unavailable";
    }
}

inline const char* connectionStateText(const NetworkSnapshot& snapshot,
                                       const NetworkInterfaceInfo* adapter)
{
    if (snapshot.state == SnapshotState::Unavailable)
        return snapshot.backend == Backend::Kernel ? "Adapter unavailable" :
            "guideXOS network service unavailable";
    if (snapshot.state == SnapshotState::NoAdapter || !adapter) return "No adapter";
    if (adapter->linkState == LinkState::Unknown) return "Adapter state unavailable";
    if (adapter->linkState == LinkState::Down) return "Disconnected";
    if (adapter->dhcpState == DhcpState::Initializing) return "Obtaining address...";
    if (adapter->dhcpState == DhcpState::FailedNoLease && !adapter->ipv4Address.available)
        return "DHCP failed; no lease";
    if (!adapter->ipv4Address.available)
        return adapter->configurationMode == ConfigurationMode::Dhcp
            ? "Link up, no lease" : "Link up, address unavailable";
    if (adapter->ipv4Address.value == 0) return "Link up; 0.0.0.0 assigned";
    return "Configured; Internet not checked";
}

} // namespace network_settings
} // namespace gxos
