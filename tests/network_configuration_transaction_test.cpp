#include "network_configuration_transaction.h"

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

struct FakeState {
    int value{10};
    int captures{0};
    int applies{0};
    int verifies{0};
    int rollbacks{0};
    int rollbackVerifies{0};
    bool failCapture{false};
    bool failApply{false};
    bool failVerify{false};
    bool failRollback{false};
    bool failRollbackVerify{false};
};

bool capture(void* context, int* output)
{
    FakeState& state = *static_cast<FakeState*>(context);
    ++state.captures;
    if (state.failCapture) return false;
    *output = state.value;
    return true;
}

bool apply(void* context, const NetworkConfigurationCandidate& candidate)
{
    FakeState& state = *static_cast<FakeState*>(context);
    ++state.applies;
    state.value = static_cast<int>(candidate.staticIPv4.address);
    return !state.failApply;
}

bool verify(void* context, const NetworkConfigurationCandidate& candidate)
{
    FakeState& state = *static_cast<FakeState*>(context);
    ++state.verifies;
    return !state.failVerify &&
        state.value == static_cast<int>(candidate.staticIPv4.address);
}

bool rollback(void* context, const int& previous)
{
    FakeState& state = *static_cast<FakeState*>(context);
    ++state.rollbacks;
    if (state.failRollback) return false;
    state.value = previous;
    return true;
}

bool verifyRollback(void* context, const int& previous)
{
    FakeState& state = *static_cast<FakeState*>(context);
    ++state.rollbackVerifies;
    return !state.failRollbackVerify && state.value == previous;
}

NetworkConfigurationCandidate staticCandidate()
{
    NetworkConfigurationCandidate candidate{};
    candidate.expectedGeneration = 11;
    candidate.interfaceId = 0x00030000u;
    copyText(candidate.stableId, sizeof(candidate.stableId), "pci:00:03.0:mac:001122334455");
    candidate.mode = NetworkMode::Static;
    candidate.dnsMode = DnsMode::Manual;
    candidate.staticIPv4 = IPv4Configuration{
        0x0A01092Au, 0xFFFFFF00u, 0x0A010901u, 0x0A010974u
    };
    return candidate;
}

ConfigurationTransactionResult run(FakeState& state,
    const NetworkConfigurationCandidate& candidate)
{
    const ConfigurationTransactionOperations<int> operations{
        &state, capture, apply, verify, rollback, verifyRollback
    };
    return runConfigurationTransaction(candidate, operations);
}

NetworkSnapshot snapshot(uint64_t generation, const char* stableId)
{
    NetworkSnapshot result{};
    result.backend = Backend::Kernel;
    result.state = SnapshotState::AdaptersAvailable;
    result.generation = generation;
    result.adapterCount = 1;
    result.adapters[0].interfaceId = 0x00030000u;
    copyText(result.adapters[0].stableId,
        sizeof(result.adapters[0].stableId), stableId);
    return result;
}
}

int main()
{
    NetworkConfigurationCandidate candidate = staticCandidate();
    check(validateCandidate(candidate) == ConfigurationField::None,
          "complete static address, mask, gateway, and DNS candidate validates");

    NetworkConfigurationCandidate dhcp{};
    dhcp.expectedGeneration = 11;
    dhcp.interfaceId = candidate.interfaceId;
    copyText(dhcp.stableId, sizeof(dhcp.stableId), candidate.stableId);
    dhcp.mode = NetworkMode::Dhcp;
    dhcp.dnsMode = DnsMode::Automatic;
    check(validateCandidate(dhcp) == ConfigurationField::None,
          "complete DHCP candidate has consistent automatic fields");
    FakeState state;
    check(run(state, dhcp).outcome == TransactionOutcome::Unsupported &&
          state.captures == 0 && state.applies == 0 && state.value == 10,
          "unsupported DHCP transition is rejected before capture or mutation");
    NetworkConfigurationCandidate automaticDns = staticCandidate();
    automaticDns.dnsMode = DnsMode::Automatic;
    automaticDns.staticIPv4.dns = 0u;
    check(validateCandidate(automaticDns) == ConfigurationField::None &&
          run(state, automaticDns).outcome == TransactionOutcome::Unsupported &&
          state.applies == 0,
          "well-formed but unsupported automatic static DNS is distinct from bad input");

    NetworkConfigurationCandidate invalid = candidate;
    invalid.staticIPv4.address = 0u;
    const int beforeAddress = state.value;
    check(run(state, invalid).outcome == TransactionOutcome::ValidationFailed &&
          state.applies == 0 && state.value == beforeAddress,
          "malformed IPv4 fails validation with zero mutation");
    invalid = candidate;
    invalid.staticIPv4.subnetMask = 0xFF00FF00u;
    check(validateCandidate(invalid) == ConfigurationField::SubnetMask,
          "noncontiguous subnet mask is rejected");
    invalid = candidate;
    invalid.staticIPv4.gateway = 0x0A010801u;
    check(validateCandidate(invalid) == ConfigurationField::Gateway,
          "off-subnet gateway is rejected");
    invalid = candidate;
    invalid.staticIPv4.dns = 0xE0000001u;
    check(validateCandidate(invalid) == ConfigurationField::Dns,
          "multicast DNS address is rejected");
    invalid = candidate;
    invalid.staticIPv4.address = 0x0A010900u;
    check(validateCandidate(invalid) == ConfigurationField::Address,
          "network address cannot be configured as a host address");

    NetworkSnapshot observed = snapshot(11, candidate.stableId);
    check(validateCandidateInterface(observed, candidate) == TransactionOutcome::Success,
          "candidate binds to the observed interface generation and identity");
    observed.generation = 12;
    check(validateCandidateInterface(observed, candidate) == TransactionOutcome::StaleInterface,
          "stale configuration generation is rejected");
    observed = snapshot(11, "pci:00:03.0:mac:aabbccddeeff");
    check(validateCandidateInterface(observed, candidate) == TransactionOutcome::MissingInterface,
          "reused interface slot with a different NIC identity is rejected");
    observed = snapshot(11, candidate.stableId);
    observed.state = SnapshotState::NoAdapter;
    observed.adapterCount = 0;
    check(validateCandidateInterface(observed, candidate) == TransactionOutcome::MissingInterface,
          "missing interface is rejected even when an old slot was selected");

    state = FakeState{};
    ConfigurationTransactionResult result = run(state, candidate);
    check(result.outcome == TransactionOutcome::Success &&
          state.value == static_cast<int>(candidate.staticIPv4.address) &&
          state.captures == 1 && state.applies == 1 && state.verifies == 1,
          "static Apply captures, applies, and verifies authoritative state");
    result = run(state, candidate);
    check(result.outcome == TransactionOutcome::Success && state.applies == 2,
          "repeated Apply is deterministic and idempotent");

    state = FakeState{};
    state.failCapture = true;
    check(run(state, candidate).outcome == TransactionOutcome::CaptureFailed &&
          state.value == 10 && state.applies == 0,
          "capture failure occurs before the first mutation");

    state = FakeState{};
    state.failApply = true;
    check(run(state, candidate).outcome == TransactionOutcome::ApplyFailedRolledBack &&
          state.value == 10 && state.rollbackVerifies == 1,
          "partial Apply failure restores and verifies the previous state");
    state = FakeState{};
    state.failApply = true;
    state.failRollback = true;
    check(run(state, candidate).outcome == TransactionOutcome::ApplyFailedRollbackFailed &&
          state.value != 10,
          "failed Apply rollback is surfaced as a distinct serious outcome");

    state = FakeState{};
    state.failVerify = true;
    check(run(state, candidate).outcome == TransactionOutcome::VerifyFailedRolledBack &&
          state.value == 10 && state.rollbackVerifies == 1,
          "verification failure restores and verifies the previous state");
    state = FakeState{};
    state.failVerify = true;
    state.failRollbackVerify = true;
    check(run(state, candidate).outcome == TransactionOutcome::VerifyFailedRollbackFailed,
          "unverified rollback is never reported as restored");

    state = FakeState{};
    const int beforeCancel = state.value;
    // Cancel discards the local staged candidate; it does not call the kernel runner.
    candidate = staticCandidate();
    check(state.value == beforeCancel && state.applies == 0,
          "Cancel without Apply leaves transaction state untouched");

    std::cout << "Network configuration transaction tests: "
              << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
