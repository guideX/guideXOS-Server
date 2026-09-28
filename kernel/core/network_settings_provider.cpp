#include "include/kernel/network_settings_provider.h"

#include "network_configuration_transaction.h"

#include "include/kernel/dhcp.h"
#include "include/kernel/dns.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/nic.h"

namespace kernel {
namespace network_settings_provider {
namespace {

using namespace gxos::network_settings;

NetworkSnapshot s_previousSnapshot{};
bool s_hasPreviousSnapshot = false;

struct PreviousNetworkConfiguration {
    ipv4::ConfigurationSnapshot ipv4State{};
    dhcp::ConfigurationState dhcpState{};
    uint32_t dnsServer{0};
};

bool sameDhcpState(const dhcp::ConfigurationState& left,
                   const dhcp::ConfigurationState& right)
{
    const dhcp::LeaseInfo& a = left.lease;
    const dhcp::LeaseInfo& b = right.lease;
    return left.state == right.state && a.assignedIP == b.assignedIP &&
        a.subnetMask == b.subnetMask && a.gateway == b.gateway &&
        a.dnsServer == b.dnsServer && a.serverIP == b.serverIP &&
        a.leaseTime == b.leaseTime && a.renewalTime == b.renewalTime &&
        a.rebindingTime == b.rebindingTime && a.leaseStartTick == b.leaseStartTick &&
        a.xid == b.xid && a.valid == b.valid;
}

bool sameIpv4State(const ipv4::ConfigurationSnapshot& left,
                   const ipv4::ConfigurationSnapshot& right)
{
    const ipv4::NetworkConfig& a = left.config;
    const ipv4::NetworkConfig& b = right.config;
    if (a.ipAddr != b.ipAddr || a.subnetMask != b.subnetMask ||
        a.gateway != b.gateway || a.dns != b.dns || a.configured != b.configured)
        return false;
    for (size_t i = 0; i < sizeof(a.macAddr); ++i)
        if (a.macAddr[i] != b.macAddr[i]) return false;
    for (size_t i = 0; i < ipv4::MAX_ROUTES; ++i) {
        const ipv4::RouteEntry& x = left.routes[i];
        const ipv4::RouteEntry& y = right.routes[i];
        if (x.network != y.network || x.mask != y.mask || x.gateway != y.gateway ||
            x.metric != y.metric || x.active != y.active) return false;
    }
    return true;
}

bool capturePrevious(void* context, PreviousNetworkConfiguration* output)
{
    if (!context || !output) return false;
    return ipv4::capture_configuration(&output->ipv4State) &&
        dhcp::capture_configuration_state(&output->dhcpState) &&
        (output->dnsServer = dns::get_server(), true);
}

bool applyCandidate(void*, const NetworkConfigurationCandidate& candidate)
{
    dhcp::enter_static_mode();
    if (ipv4::replace_configuration(candidate.staticIPv4.address,
            candidate.staticIPv4.subnetMask, candidate.staticIPv4.gateway,
            candidate.staticIPv4.dns) != ipv4::IP_OK) return false;
    dns::set_server(candidate.staticIPv4.dns);
    return true;
}

bool verifyCandidate(void*, const NetworkConfigurationCandidate& candidate)
{
    return candidate.mode == NetworkMode::Static &&
        candidate.dnsMode == DnsMode::Manual &&
        ipv4::configuration_matches(candidate.staticIPv4.address,
            candidate.staticIPv4.subnetMask, candidate.staticIPv4.gateway,
            candidate.staticIPv4.dns) &&
        dns::get_server() == candidate.staticIPv4.dns &&
        dhcp::static_mode_is_active();
}

bool rollbackPrevious(void* context, const PreviousNetworkConfiguration& previous)
{
    if (!context) return false;
    const bool ipv4Restored = ipv4::restore_configuration(&previous.ipv4State);
    dns::set_server(previous.dnsServer);
    dhcp::restore_configuration_state(&previous.dhcpState);
    return ipv4Restored;
}

bool verifyRollback(void* context, const PreviousNetworkConfiguration& previous)
{
    if (!context) return false;
    ipv4::ConfigurationSnapshot currentIpv4{};
    dhcp::ConfigurationState currentDhcp{};
    return ipv4::capture_configuration(&currentIpv4) &&
        dhcp::capture_configuration_state(&currentDhcp) &&
        sameIpv4State(currentIpv4, previous.ipv4State) &&
        sameDhcpState(currentDhcp, previous.dhcpState) &&
        dns::get_server() == previous.dnsServer;
}

void appendHex(char* output, size_t& at, uint8_t value)
{
    static const char digits[] = "0123456789abcdef";
    output[at++] = digits[(value >> 4) & 0x0Fu];
    output[at++] = digits[value & 0x0Fu];
}

void makeStableId(const nic::NICDevice& device, NetworkInterfaceInfo& output)
{
    size_t at = 0;
    const char prefix[] = "pci:";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) output.stableId[at++] = prefix[i];
    appendHex(output.stableId, at, device.pciBus);
    output.stableId[at++] = ':';
    appendHex(output.stableId, at, device.pciSlot);
    output.stableId[at++] = '.';
    output.stableId[at++] = static_cast<char>('0' + (device.pciFunc % 10));
    const char macLabel[] = ":mac:";
    for (size_t i = 0; i < sizeof(macLabel) - 1; ++i)
        output.stableId[at++] = macLabel[i];
    for (size_t i = 0; i < nic::ETH_ALEN; ++i)
        appendHex(output.stableId, at, device.macAddress[i]);
    output.stableId[at] = '\0';
    output.interfaceId = (static_cast<uint32_t>(device.pciBus) << 16) |
        (static_cast<uint32_t>(device.pciSlot) << 8) | device.pciFunc;
}

const char* driverName(const nic::NICDevice& device)
{
    if (device.vendorId != nic::PCI_VENDOR_INTEL) return "Unknown network driver";
    if (device.deviceId == nic::PCI_DEVICE_E1000) return "Intel 82540EM E1000";
    if (device.deviceId == nic::PCI_DEVICE_E1000E) return "Intel E1000E";
    if (device.deviceId == nic::PCI_DEVICE_I217) return "Intel I217-LM";
    return "Intel E1000 family";
}

void captureDhcp(NetworkInterfaceInfo& adapter, bool leaseValid, dhcp::ClientState state)
{
    switch (state) {
    case dhcp::STATE_SELECTING:
    case dhcp::STATE_REQUESTING:
        adapter.dhcpState = DhcpState::Initializing;
        adapter.configurationMode = ConfigurationMode::Dhcp;
        break;
    case dhcp::STATE_BOUND:
        adapter.dhcpState = leaseValid ? DhcpState::LeaseAcquired : DhcpState::FailedNoLease;
        adapter.configurationMode = leaseValid ? ConfigurationMode::Dhcp : ConfigurationMode::Unknown;
        break;
    case dhcp::STATE_RENEWING:
        adapter.dhcpState = DhcpState::LeaseRenewing;
        adapter.configurationMode = ConfigurationMode::Dhcp;
        break;
    case dhcp::STATE_REBINDING:
        adapter.dhcpState = DhcpState::LeaseRebinding;
        adapter.configurationMode = ConfigurationMode::Dhcp;
        break;
    case dhcp::STATE_ERROR:
        adapter.dhcpState = DhcpState::FailedNoLease;
        adapter.configurationMode = ConfigurationMode::Static;
        break;
    case dhcp::STATE_INIT:
    case dhcp::STATE_RELEASED:
        adapter.dhcpState = DhcpState::NotRunning;
        adapter.configurationMode = ConfigurationMode::Static;
        break;
    default:
        adapter.dhcpState = DhcpState::Unknown;
        adapter.configurationMode = ConfigurationMode::Unknown;
        break;
    }
}

void updateGeneration(NetworkSnapshot& snapshot)
{
    snapshot.generation = 0;
    if (!s_hasPreviousSnapshot) {
        snapshot.generation = 1;
        s_previousSnapshot = snapshot;
        s_hasPreviousSnapshot = true;
        return;
    }
    NetworkSnapshot previous = s_previousSnapshot;
    previous.generation = 0;
    if (sameSnapshot(previous, snapshot)) {
        snapshot.generation = s_previousSnapshot.generation;
        return;
    }
    snapshot.generation = s_previousSnapshot.generation + 1;
    if (snapshot.generation == 0) snapshot.generation = 1;
    s_previousSnapshot = snapshot;
}

} // namespace

namespace {

gxos::network_settings::Result appModelReadSnapshot(
    void*, gxos::network_settings::NetworkSnapshot* output)
{
    return readSnapshot(output);
}

} // namespace

gxos::network_settings::Provider appModelProvider()
{
    return gxos::network_settings::Provider{ nullptr, appModelReadSnapshot };
}

gxos::network_settings::Result readSnapshot(
    gxos::network_settings::NetworkSnapshot* output)
{
    using namespace gxos::network_settings;
    if (!output) return Result::InvalidArgument;

    NetworkSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;
    switch (nic::get_probe_state()) {
    case nic::NIC_PROBE_NO_SUPPORTED_DEVICE:
        snapshot.state = SnapshotState::NoAdapter;
        updateGeneration(snapshot);
        *output = snapshot;
        return Result::Ok;
    case nic::NIC_PROBE_UNINITIALIZED:
    case nic::NIC_PROBE_UNSUPPORTED_ARCHITECTURE:
        snapshot.state = SnapshotState::Unavailable;
        updateGeneration(snapshot);
        *output = snapshot;
        return Result::Unavailable;
    case nic::NIC_PROBE_DEVICE_UNAVAILABLE:
        snapshot.state = SnapshotState::Unavailable;
        updateGeneration(snapshot);
        *output = snapshot;
        return Result::Unavailable;
    case nic::NIC_PROBE_READY:
        break;
    default:
        snapshot.state = SnapshotState::Unavailable;
        updateGeneration(snapshot);
        *output = snapshot;
        return Result::Unavailable;
    }

    const nic::NICDevice* device = nic::get_device();
    if (!device || !device->active) {
        snapshot.state = SnapshotState::Unavailable;
        updateGeneration(snapshot);
        *output = snapshot;
        return Result::Unavailable;
    }

    snapshot.state = SnapshotState::AdaptersAvailable;
    snapshot.adapterCount = 1;
    NetworkInterfaceInfo& adapter = snapshot.adapters[0];
    makeStableId(*device, adapter);
    copyText(adapter.name, sizeof(adapter.name), device->name);
    copyText(adapter.driver, sizeof(adapter.driver), driverName(*device));
    adapter.linkState = nic::get_link_state() == nic::NIC_LINK_UP ? LinkState::Up : LinkState::Down;

    const dhcp::LeaseInfo* lease = dhcp::get_lease();
    const bool leaseValid = lease && lease->valid;
    captureDhcp(adapter, leaseValid, dhcp::get_state());

    const ipv4::NetworkConfig* config = ipv4::get_config();
    if (config && config->configured) {
        adapter.ipv4Address = IPv4Value{ config->ipAddr, true };
        adapter.subnetMask = IPv4Value{ config->subnetMask, true };
        adapter.gateway = IPv4Value{ config->gateway, true };
        const uint32_t dnsServer = dns::get_server();
        adapter.dns = IPv4Value{ dnsServer, dnsServer != 0u };
        if (leaseValid && adapter.dhcpState == DhcpState::LeaseAcquired)
            adapter.dnsSource = DnsSource::Dhcp;
        else
            adapter.dnsSource = dnsServer != 0u ? DnsSource::Static : DnsSource::Unknown;
    } else {
        adapter.configurationMode = ConfigurationMode::Unknown;
        adapter.ipv4Address = IPv4Value{};
        adapter.subnetMask = IPv4Value{};
        adapter.gateway = IPv4Value{};
        adapter.dns = IPv4Value{};
        adapter.dnsSource = DnsSource::Unknown;
    }

    updateGeneration(snapshot);
    *output = snapshot;
    return Result::Ok;
}

gxos::network_settings::ConfigurationTransactionResult applyCandidateLocally(
    const gxos::network_settings::NetworkConfigurationCandidate& candidate)
{
    using namespace gxos::network_settings;
    ConfigurationTransactionResult result{};
    const ConfigurationField invalid = validateCandidate(candidate);
    if (invalid != ConfigurationField::None) {
        result.outcome = TransactionOutcome::ValidationFailed;
        result.field = invalid;
        return result;
    }

    NetworkSnapshot current{};
    const Result readResult = readSnapshot(&current);
    result.resultingGeneration = current.generation;
    if (candidate.expectedGeneration != current.generation) {
        result.outcome = TransactionOutcome::StaleInterface;
        result.field = ConfigurationField::Generation;
        return result;
    }
    if (readResult != Result::Ok || current.state != SnapshotState::AdaptersAvailable) {
        result.outcome = TransactionOutcome::MissingInterface;
        return result;
    }
    result.outcome = validateCandidateInterface(current, candidate);
    if (result.outcome != TransactionOutcome::Success) {
        result.field = result.outcome == TransactionOutcome::StaleInterface
            ? ConfigurationField::Generation : ConfigurationField::Interface;
        return result;
    }

    const NetworkInterfaceInfo* selected = nullptr;
    for (uint32_t i = 0; i < current.adapterCount; ++i) {
        if (current.adapters[i].interfaceId == candidate.interfaceId) {
            selected = &current.adapters[i];
            break;
        }
    }
    if (!selected) {
        result.outcome = TransactionOutcome::MissingInterface;
        result.field = ConfigurationField::Interface;
        return result;
    }
    const nic::NICDevice* liveDevice = nic::get_device();
    if (nic::get_probe_state() != nic::NIC_PROBE_READY || !liveDevice ||
        !liveDevice->active) {
        result.outcome = TransactionOutcome::MissingInterface;
        result.field = ConfigurationField::Interface;
        return result;
    }
    NetworkInterfaceInfo liveIdentity{};
    makeStableId(*liveDevice, liveIdentity);
    if (liveIdentity.interfaceId != candidate.interfaceId ||
        !sameInterfaceIdentity(candidate.stableId, liveIdentity.stableId,
            kInterfaceIdBytes)) {
        result.outcome = TransactionOutcome::MissingInterface;
        result.field = ConfigurationField::Interface;
        return result;
    }

    int transactionContext = 1;
    const ConfigurationTransactionOperations<PreviousNetworkConfiguration> operations{
        &transactionContext, capturePrevious, applyCandidate, verifyCandidate,
        rollbackPrevious, verifyRollback
    };
    result = runConfigurationTransaction(candidate, operations);
    NetworkSnapshot resulting{};
    if (readSnapshot(&resulting) == Result::Ok ||
        resulting.generation != 0u) {
        result.resultingGeneration = resulting.generation;
    }
    result.snapshotRefreshRequired = true;
    return result;
}

} // namespace network_settings_provider
} // namespace kernel
