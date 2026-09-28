#include "network_settings_contract.h"
#include "settings_network_service.h"

#include <cstring>
#include <iostream>

using namespace gxos::network_settings;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, const char* name)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

struct TestProvider {
    NetworkSnapshot snapshot{};
    Result result{Result::Ok};
    uint32_t calls{0};
};

Result readTestSnapshot(void* context, NetworkSnapshot* output)
{
    if (!context || !output) return Result::InvalidArgument;
    TestProvider& provider = *static_cast<TestProvider*>(context);
    ++provider.calls;
    *output = provider.snapshot;
    return provider.result;
}

NetworkInterfaceInfo makeAdapter(uint32_t id, const char* name)
{
    NetworkInterfaceInfo adapter{};
    adapter.interfaceId = id;
    copyText(adapter.stableId, sizeof(adapter.stableId), name);
    copyText(adapter.name, sizeof(adapter.name), name);
    copyText(adapter.driver, sizeof(adapter.driver), "deterministic test driver");
    adapter.linkState = LinkState::Down;
    return adapter;
}

IPv4ConfigurationInput validConfiguration()
{
    IPv4ConfigurationInput input{};
    copyText(input.address, sizeof(input.address), "10.1.9.42");
    copyText(input.subnetMask, sizeof(input.subnetMask), "255.255.255.0");
    copyText(input.gateway, sizeof(input.gateway), "10.1.9.1");
    copyText(input.dns, sizeof(input.dns), "10.1.9.116");
    return input;
}
}

int main()
{
    TestProvider providerState;
    Provider provider{ &providerState, readTestSnapshot };
    NetworkSnapshot output{};

    providerState.snapshot.backend = Backend::HostedTest;
    providerState.snapshot.state = SnapshotState::NoAdapter;
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Ok &&
          output.adapterCount == 0 && output.state == SnapshotState::NoAdapter,
          "zero adapters is an explicit no-adapter state");

    providerState.snapshot.state = SnapshotState::AdaptersAvailable;
    providerState.snapshot.adapterCount = 1;
    providerState.snapshot.adapters[0] = makeAdapter(41, "eth-test");
    providerState.snapshot.adapters[0].linkState = LinkState::Up;
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Ok &&
          output.adapterCount == 1 && output.adapters[0].interfaceId == 41,
          "one adapter is copied into the bounded snapshot");
    check(std::strcmp(output.adapters[0].name, "eth-test") == 0 &&
          std::strcmp(output.adapters[0].driver, "deterministic test driver") == 0,
          "adapter identity is structured and copied without pointers");

    providerState.snapshot.adapterCount = 3;
    providerState.snapshot.adapters[1] = makeAdapter(42, "eth-second");
    providerState.snapshot.adapters[2] = makeAdapter(43, "eth-third");
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Ok &&
          output.adapterCount == 3 && output.adapters[2].interfaceId == 43,
          "multiple adapters remain independently representable");

    providerState.snapshot.adapterCount = static_cast<uint32_t>(kMaxAdapters);
    providerState.snapshot.adapters[3] = makeAdapter(44, "eth-fourth");
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Ok &&
          output.adapterCount == kMaxAdapters && !output.truncated,
          "adapter capacity boundary is accepted");
    providerState.snapshot.adapterCount = static_cast<uint32_t>(kMaxAdapters + 1);
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Ok &&
          output.adapterCount == kMaxAdapters && output.truncated,
          "excess adapters are clamped and marked truncated");

    providerState.snapshot.adapterCount = 1;
    NetworkInterfaceInfo& adapter = providerState.snapshot.adapters[0];
    adapter = makeAdapter(51, "eth0");
    check(std::strcmp(connectionStateText(providerState.snapshot, &adapter), "Disconnected") == 0,
          "link down remains disconnected");
    adapter.linkState = LinkState::Up;
    check(std::strcmp(connectionStateText(providerState.snapshot, &adapter), "Link up, no lease") == 0,
          "link up without an address is not called connected");
    adapter.configurationMode = ConfigurationMode::Dhcp;
    adapter.dhcpState = DhcpState::Initializing;
    check(std::strcmp(connectionStateText(providerState.snapshot, &adapter), "Obtaining address...") == 0,
          "DHCP initialization is visible");
    adapter.dhcpState = DhcpState::FailedNoLease;
    check(std::strcmp(connectionStateText(providerState.snapshot, &adapter), "DHCP failed; no lease") == 0,
          "DHCP failure is distinct from link state");
    adapter.dhcpState = DhcpState::LeaseAcquired;
    adapter.ipv4Address = IPv4Value{ 0x0A01092Au, true };
    adapter.configurationMode = ConfigurationMode::Dhcp;
    check(std::strcmp(connectionStateText(providerState.snapshot, &adapter), "Connected") == 0,
          "addressed DHCP lease is connected");
    adapter.configurationMode = ConfigurationMode::Static;
    adapter.dhcpState = DhcpState::NotRunning;
    check(adapter.configurationMode == ConfigurationMode::Static,
          "static assignment remains distinct from DHCP");

    providerState.snapshot.state = SnapshotState::Unavailable;
    providerState.snapshot.adapterCount = 0;
    providerState.result = Result::Unavailable;
    check(readSnapshot(provider, Client::BuiltInSettings, &output) == Result::Unavailable &&
          output.state == SnapshotState::Unavailable,
          "unavailable provider state is preserved");
    const Provider hostedProvider = gxos::apps::settings::hostedGuideXosNetworkProvider();
    check(gxos::apps::settings::readSettingsNetworkSnapshot(hostedProvider, &output) == Result::Unavailable &&
          output.backend == Backend::Unavailable && output.state == SnapshotState::Unavailable,
          "hosted Settings provider never substitutes host network data");
    check(std::strcmp(connectionStateText(output, nullptr),
                       "Kernel state unavailable in hosted Settings") == 0,
          "hosted kernel unavailability is labeled explicitly");
    output.backend = Backend::Kernel;
    check(std::strcmp(connectionStateText(output, nullptr), "Adapter unavailable") == 0,
          "kernel adapter failure is distinguished from hosted isolation");

    check(isAuthorized(Client::BuiltInSettings, Operation::ReadStatus),
          "built-in Settings can request read-only status");
    check(!isAuthorized(Client::OrdinaryApplication, Operation::ReadStatus),
          "ordinary App Model application cannot request status through this service");
    check(requestConfiguration(Client::OrdinaryApplication) == Result::Unauthorized,
          "ordinary App Model application cannot change network configuration");
    check(requestConfiguration(Client::BuiltInSettings) == Result::Unsupported,
          "Settings is trusted for changes but lower-level atomic configuration is unsupported");
    const uint32_t callsBeforeDeniedRead = providerState.calls;
    check(readSnapshot(provider, Client::OrdinaryApplication, &output) == Result::Unauthorized &&
          providerState.calls == callsBeforeDeniedRead,
          "unauthorized read does not reach the provider");

    uint32_t parsed = 0;
    check(parseIPv4("10.1.9.42", &parsed) && parsed == 0x0A01092Au,
          "strict IPv4 parser accepts a valid address");
    check(!parseIPv4("10.1.9.256", &parsed) && !parseIPv4("10.1.9", &parsed) &&
          !parseIPv4("10.1.9.1x", &parsed) && !parseIPv4("10..9.1", &parsed),
          "strict IPv4 parser rejects malformed addresses");

    IPv4Configuration validated{ 1, 2, 3, 4 };
    IPv4Configuration before = validated;
    IPv4ConfigurationInput input = validConfiguration();
    check(validateStaticIPv4(input, &validated) == ValidationField::None &&
          validated.address == 0x0A01092Au && validated.gateway == 0x0A010901u &&
          validated.dns == 0x0A010974u,
          "static IPv4 input validates as one staged configuration");
    input = validConfiguration();
    copyText(input.address, sizeof(input.address), "10.1.9.999");
    check(validateStaticIPv4(input, &validated) == ValidationField::Address,
          "malformed static IPv4 address is rejected");
    validated = before;
    input = validConfiguration();
    copyText(input.subnetMask, sizeof(input.subnetMask), "255.0.255.0");
    check(validateStaticIPv4(input, &validated) == ValidationField::SubnetMask &&
          validated.address == before.address && validated.subnetMask == before.subnetMask,
          "malformed subnet mask is rejected without mutating the staged output");
    input = validConfiguration();
    copyText(input.gateway, sizeof(input.gateway), "10.1.9.x");
    check(validateStaticIPv4(input, &validated) == ValidationField::Gateway,
          "malformed gateway is rejected");
    input = validConfiguration();
    copyText(input.gateway, sizeof(input.gateway), "10.1.8.1");
    check(validateStaticIPv4(input, &validated) == ValidationField::Gateway,
          "gateway outside the configured subnet is rejected");
    input = validConfiguration();
    copyText(input.dns, sizeof(input.dns), "10.1.9.999");
    check(validateStaticIPv4(input, &validated) == ValidationField::Dns,
          "malformed DNS address is rejected");
    check(isContiguousSubnetMask(0xFFFFFF00u) && isContiguousSubnetMask(0xFFFFFFFFu) &&
          !isContiguousSubnetMask(0xFF00FF00u),
          "only contiguous subnet masks are accepted");

    NetworkSnapshot generationSnapshot{};
    generationSnapshot.state = SnapshotState::AdaptersAvailable;
    generationSnapshot.generation = 8;
    generationSnapshot.adapterCount = 1;
    generationSnapshot.adapters[0] = makeAdapter(61, "pci:00:03.0");
    NetworkInterfaceInfo selected{};
    check(adapterForGeneration(generationSnapshot, 61, 8, &selected),
          "adapter handle resolves at its observed generation");
    check(!adapterForGeneration(generationSnapshot, 61, 7, &selected),
          "stale adapter generation is rejected after disappearance or replacement");
    generationSnapshot.generation = 9;
    generationSnapshot.state = SnapshotState::NoAdapter;
    generationSnapshot.adapterCount = 0;
    check(!adapterForGeneration(generationSnapshot, 61, 8, &selected),
          "disappeared adapter cannot be selected from a newer snapshot");

    RefreshPolicy refresh;
    refresh.setActive(true, 100);
    check(refresh.active() && refresh.due(100), "entering the visible Network page schedules an immediate refresh");
    refresh.completed(100);
    check(!refresh.due(2099) && refresh.due(2100), "network refresh interval is bounded to two seconds");
    refresh.setActive(false, 2100);
    check(!refresh.active() && !refresh.due(5000), "leaving the page or losing focus stops refresh work");
    refresh.setActive(true, 5000);
    check(refresh.due(5000), "re-entering the Network page refreshes immediately");
    refresh.completed(5000);
    refresh.setActive(false, 5100);
    refresh.setActive(true, 6000);
    check(refresh.due(6000), "close and reopen restarts with a fresh bounded schedule");

    std::cout << "Network Settings contract tests: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
