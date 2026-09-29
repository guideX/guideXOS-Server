#include "system_service_client.h"
#include "system_service_qemu_transport.h"

#include <cstdlib>
#include <iostream>

namespace {

bool parsePort(const char* text, uint16_t* output)
{
    if (!text || !output || !text[0]) return false;
    unsigned long value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10u + static_cast<unsigned long>(*p - '0');
        if (value > 65535u) return false;
    }
    if (value == 0) return false;
    *output = static_cast<uint16_t>(value);
    return true;
}

void printHex64(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    char buffer[16];
    for (int i = 15; i >= 0; --i) {
        buffer[i] = digits[value & 0x0Fu];
        value >>= 4;
    }
    std::cout.write(buffer, sizeof(buffer));
}

void printHex32(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4)
        std::cout << digits[(value >> shift) & 0x0Fu];
}

void printHex8(uint8_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    std::cout << digits[(value >> 4) & 0x0Fu] << digits[value & 0x0Fu];
}

void printAddress(const char* label, const gxos::network_settings::IPv4Value& value)
{
    std::cout << ' ' << label << '=';
    if (!value.available) {
        std::cout << "unavailable";
        return;
    }
    char address[16]{};
    if (gxos::network_settings::formatIPv4(value.value, address))
        std::cout << address;
    else
        std::cout << "unavailable";
}

void printSnapshot(uint32_t requestNumber,
                   const gxos::network_settings::NetworkSnapshot& snapshot)
{
    std::cout << "SYSCLIENT snapshot received request=" << requestNumber
        << " generation=";
    printHex64(snapshot.generation);
    std::cout << " state=";
    printHex8(static_cast<uint8_t>(snapshot.state));
    std::cout << " adapters=";
    printHex32(snapshot.adapterCount);
    std::cout << " backend=";
    printHex8(static_cast<uint8_t>(snapshot.backend));
    if (snapshot.adapterCount > 0) {
        const auto& adapter = snapshot.adapters[0];
        std::cout << " name=" << adapter.name << " driver=" << adapter.driver;
        std::cout << " link=";
        printHex8(static_cast<uint8_t>(adapter.linkState));
        std::cout << " mode=";
        printHex8(static_cast<uint8_t>(adapter.configurationMode));
        std::cout << " dhcp=";
        printHex8(static_cast<uint8_t>(adapter.dhcpState));
        std::cout << " dnsSource=";
        printHex8(static_cast<uint8_t>(adapter.dnsSource));
        printAddress("ipv4", adapter.ipv4Address);
        printAddress("mask", adapter.subnetMask);
        printAddress("gateway", adapter.gateway);
        printAddress("dns", adapter.dns);
    }
    std::cout << "\n";
}

void printDeviceSnapshot(uint32_t requestNumber,
                         const gxos::settings_inventory::DeviceSnapshot& snapshot)
{
    using namespace gxos::settings_inventory;
    std::cout << "SYSCLIENT devices received request=" << requestNumber << " generation=";
    printHex64(snapshot.generation);
    std::cout << " devices=";
    printHex32(snapshot.deviceCount);
    std::cout << " total=";
    printHex32(snapshot.totalDeviceCount);
    std::cout << " truncated=";
    printHex8(snapshot.truncated ? 1u : 0u);
    std::cout << " backend=";
    printHex8(static_cast<uint8_t>(snapshot.backend));
    if (snapshot.deviceCount != 0u) {
        const DeviceInfo& device = snapshot.devices[0];
        std::cout << " id=" << device.stableId << " category=";
        printHex8(static_cast<uint8_t>(device.category));
        std::cout << " status=";
        printHex8(static_cast<uint8_t>(device.status));
    }
    std::cout << "\n";
}

void printStorageSnapshot(uint32_t requestNumber,
                          const gxos::settings_inventory::StorageSnapshot& snapshot)
{
    using namespace gxos::settings_inventory;
    std::cout << "SYSCLIENT storage received request=" << requestNumber << " generation=";
    printHex64(snapshot.generation);
    std::cout << " disks=";
    printHex32(snapshot.diskCount);
    std::cout << " volumes=";
    printHex32(snapshot.volumeCount);
    std::cout << " truncated=";
    printHex8(snapshot.truncated ? 1u : 0u);
    std::cout << " backend=";
    printHex8(static_cast<uint8_t>(snapshot.backend));
    if (snapshot.diskCount != 0u) {
        const DiskInfo& disk = snapshot.disks[0];
        std::cout << " diskId=" << disk.stableId << " bytes=";
        printHex64(disk.capacityBytes);
        std::cout << " sector=";
        printHex32(disk.sectorSize);
        std::cout << " partitions=";
        printHex8(disk.partitionCount);
        std::cout << " table=";
        printHex8(static_cast<uint8_t>(disk.partitionTable));
    }
    std::cout << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    uint16_t port = gxos::system_service::kDefaultQemuCom2Port;
    if (argc > 1 && !parsePort(argv[1], &port)) {
        std::cerr << "Invalid service port.\n";
        return 2;
    }

    gxos::system_service::QemuCom2TcpTransport transport(port);
    gxos::system_service::SystemServiceClient client(transport.asTransport(), 1500u);
    gxos::network_settings::NetworkSnapshot lastSnapshot{};
    for (uint32_t requestNumber = 1; requestNumber <= 2; ++requestNumber) {
        gxos::network_settings::NetworkSnapshot snapshot{};
        const gxos::system_service::ClientResult result =
            client.getNetworkSnapshot(&snapshot);
        if (result != gxos::system_service::ClientResult::Ok ||
            snapshot.backend != gxos::network_settings::Backend::Kernel ||
            snapshot.generation == 0u || snapshot.adapterCount != 1u) {
            std::cerr << "SYSCLIENT request=" << requestNumber
                << " failed result=" << static_cast<unsigned>(result)
                << " backend=" << static_cast<unsigned>(snapshot.backend)
                << " generation=" << snapshot.generation << "\n";
            return 1;
        }
        lastSnapshot = snapshot;
        printSnapshot(requestNumber, snapshot);
    }

    gxos::network_settings::NetworkConfigurationCandidate candidate{};
    candidate.expectedGeneration = lastSnapshot.generation;
    candidate.interfaceId = lastSnapshot.adapters[0].interfaceId;
    gxos::network_settings::copyText(candidate.stableId, sizeof(candidate.stableId),
        lastSnapshot.adapters[0].stableId);
    candidate.mode = gxos::network_settings::NetworkMode::Static;
    candidate.dnsMode = gxos::network_settings::DnsMode::Manual;
    candidate.staticIPv4 = gxos::network_settings::IPv4Configuration{
        0x0A01092Au, 0xFFFFFF00u, 0x0A010901u, 0x0A010974u
    };
    const gxos::system_service::ClientResult mutationResult =
        client.setNetworkConfiguration(candidate);
    if (mutationResult != gxos::system_service::ClientResult::Unauthorized) {
        std::cerr << "SYSCLIENT mutation request=3 must be unauthorized; result="
            << static_cast<unsigned>(mutationResult) << "\n";
        return 1;
    }
    std::cout << "SYSCLIENT mutation received request=3 result=unauthorized\n";

    gxos::network_settings::NetworkSnapshot afterDeniedMutation{};
    if (client.getNetworkSnapshot(&afterDeniedMutation) !=
            gxos::system_service::ClientResult::Ok ||
        !gxos::network_settings::sameSnapshot(lastSnapshot, afterDeniedMutation)) {
        std::cerr << "SYSCLIENT denied mutation changed authoritative network state\n";
        return 1;
    }
    printSnapshot(4u, afterDeniedMutation);

    gxos::settings_inventory::DeviceSnapshot devices{};
    if (client.getDeviceSnapshot(&devices) != gxos::system_service::ClientResult::Ok ||
        devices.backend != gxos::settings_inventory::Backend::Kernel ||
        devices.generation == 0u || devices.deviceCount == 0u) {
        std::cerr << "SYSCLIENT device snapshot failed or contained no guideXOS devices"
            << " backend=" << static_cast<unsigned>(devices.backend)
            << " generation=" << devices.generation
            << " devices=" << devices.deviceCount << "\n";
        return 1;
    }
    printDeviceSnapshot(5u, devices);

    gxos::settings_inventory::StorageSnapshot storage{};
    if (client.getStorageSnapshot(&storage) != gxos::system_service::ClientResult::Ok ||
        storage.backend != gxos::settings_inventory::Backend::Kernel ||
        storage.generation == 0u || storage.diskCount == 0u ||
        (storage.disks[0].flags & gxos::settings_inventory::kDiskFlagCapacityAvailable) == 0u ||
        (storage.disks[0].flags & gxos::settings_inventory::kDiskFlagSectorSizeAvailable) == 0u) {
        std::cerr << "SYSCLIENT storage snapshot failed or lacked an identifiable disk capacity"
            << " backend=" << static_cast<unsigned>(storage.backend)
            << " generation=" << storage.generation
            << " disks=" << storage.diskCount << "\n";
        return 1;
    }
    printStorageSnapshot(6u, storage);
    return 0;
}
