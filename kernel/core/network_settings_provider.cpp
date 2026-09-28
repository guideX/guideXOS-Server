#include "include/kernel/network_settings_provider.h"

#include "include/kernel/dhcp.h"
#include "include/kernel/ipv4.h"
#include "include/kernel/nic.h"

namespace kernel {
namespace network_settings_provider {
namespace {

using namespace gxos::network_settings;

NetworkSnapshot s_previousSnapshot{};
bool s_hasPreviousSnapshot = false;

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
        adapter.dns = IPv4Value{ config->dns, true };
        if (leaseValid && adapter.dhcpState == DhcpState::LeaseAcquired)
            adapter.dnsSource = DnsSource::Dhcp;
        else
            adapter.dnsSource = DnsSource::Unknown;
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

} // namespace network_settings_provider
} // namespace kernel
