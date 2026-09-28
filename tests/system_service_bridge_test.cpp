#include "network_settings_contract.h"
#include "network_telemetry.h"
#include "settings_network_service.h"
#include "system_service_client.h"
#include "system_service_dispatcher.h"

#include <chrono>
#include <cstring>
#include <iostream>

using namespace gxos::network_settings;
using namespace gxos::system_service;

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

NetworkSnapshot makeKernelSnapshot(uint64_t generation, uint32_t address)
{
    NetworkSnapshot snapshot{};
    snapshot.backend = Backend::Kernel;
    snapshot.state = SnapshotState::AdaptersAvailable;
    snapshot.generation = generation;
    snapshot.adapterCount = 1;
    NetworkInterfaceInfo& adapter = snapshot.adapters[0];
    adapter.interfaceId = 0x00030000u;
    copyText(adapter.stableId, sizeof(adapter.stableId), "pci:00:03.0");
    copyText(adapter.name, sizeof(adapter.name), "eth0");
    copyText(adapter.driver, sizeof(adapter.driver), "Intel 82540EM E1000");
    adapter.linkState = LinkState::Up;
    adapter.configurationMode = ConfigurationMode::Dhcp;
    adapter.dhcpState = DhcpState::LeaseAcquired;
    adapter.dnsSource = DnsSource::Dhcp;
    adapter.ipv4Address = IPv4Value{ address, true };
    adapter.subnetMask = IPv4Value{ 0xFFFFFF00u, true };
    adapter.gateway = IPv4Value{ 0x0A000202u, true };
    adapter.dns = IPv4Value{ 0x0A000203u, true };
    return snapshot;
}

NetworkConfigurationCandidate makeStaticCandidate(uint64_t generation)
{
    NetworkConfigurationCandidate candidate{};
    candidate.expectedGeneration = generation;
    candidate.interfaceId = 0x00030000u;
    copyText(candidate.stableId, sizeof(candidate.stableId), "pci:00:03.0");
    candidate.mode = NetworkMode::Static;
    candidate.dnsMode = DnsMode::Manual;
    candidate.staticIPv4 = IPv4Configuration{
        0x0A01092Au, 0xFFFFFF00u, 0x0A010901u, 0x0A010974u
    };
    return candidate;
}

struct ProviderState {
    NetworkSnapshot snapshot{};
    Result result{Result::Ok};
    uint32_t calls{0};
};

Result readSnapshot(void* context, NetworkSnapshot* output)
{
    if (!context || !output) return Result::InvalidArgument;
    ProviderState& state = *static_cast<ProviderState*>(context);
    ++state.calls;
    *output = state.snapshot;
    return state.result;
}

enum class TransportFault {
    None,
    Timeout,
    Disconnected,
    Truncated,
    BadResponseVersion,
    WrongRequestId,
    Oversized,
    ClosePending
};

struct TransportState {
    Provider provider{};
    DispatchTrust trust{DispatchTrust::TrustedSystemServicePeer};
    TransportFault fault{TransportFault::None};
    uint32_t transactions{0};
    uint32_t lastTimeoutMs{0};
    bool closeRequested{false};
};

TransportResult fakeTransact(void* context, const uint8_t* request,
                             size_t requestBytes, uint8_t* response,
                             size_t responseCapacity, size_t* responseBytes,
                             uint32_t timeoutMs)
{
    if (!context || !responseBytes) return TransportResult::Failed;
    TransportState& state = *static_cast<TransportState*>(context);
    ++state.transactions;
    state.lastTimeoutMs = timeoutMs;
    if (state.fault == TransportFault::Timeout) return TransportResult::Timeout;
    if (state.fault == TransportFault::Disconnected) return TransportResult::Disconnected;
    if (state.fault == TransportFault::ClosePending) {
        state.closeRequested = true;
        return TransportResult::Timeout;
    }

    uint8_t requestCopy[kMaxRequestBytes]{};
    if (!request || requestBytes > sizeof(requestCopy)) return TransportResult::Failed;
    for (size_t i = 0; i < requestBytes; ++i) requestCopy[i] = request[i];
    const bool dispatched = dispatch(requestCopy, requestBytes, state.provider,
        state.trust, response, responseCapacity, responseBytes);
    if (!dispatched) return TransportResult::Failed;
    if (state.fault == TransportFault::Truncated && *responseBytes > 0) --*responseBytes;
    else if (state.fault == TransportFault::BadResponseVersion) response[4] = 0xFFu;
    else if (state.fault == TransportFault::WrongRequestId) response[8] ^= 0x01u;
    else if (state.fault == TransportFault::Oversized) *responseBytes = responseCapacity + 1;
    return TransportResult::Ok;
}

} // namespace

int main()
{
    uint8_t request[kMaxRequestBytes]{};
    size_t requestBytes = 0;
    check(encodeRequest(kProtocolVersion,
              static_cast<uint16_t>(RequestType::GetNetworkSnapshot),
              77u, 0u, request, sizeof(request), &requestBytes) &&
          requestBytes == kRequestHeaderBytes,
          "network snapshot request uses the exact bounded wire header");
    RequestHeader decodedRequest{};
    check(decodeRequest(request, requestBytes, &decodedRequest) &&
          decodedRequest.version == kProtocolVersion && decodedRequest.requestId == 77u &&
          decodedRequest.type == static_cast<uint16_t>(RequestType::GetNetworkSnapshot),
          "valid request header decodes without pointers or host-sized fields");
    check(!decodeRequest(request, requestBytes - 1, &decodedRequest),
          "truncated request is rejected");
    check(!decodeRequest(request, requestBytes + 1, &decodedRequest),
          "oversized request is rejected");

    NetworkConfigurationCandidate candidate = makeStaticCandidate(12u);
    uint8_t configurationRequest[kMaxRequestBytes]{};
    size_t configurationRequestBytes = 0;
    NetworkConfigurationCandidate decodedCandidate{};
    check(encodeConfigurationRequest(candidate, 88u, configurationRequest,
              sizeof(configurationRequest), &configurationRequestBytes) &&
          configurationRequestBytes == kRequestHeaderBytes + kConfigurationCandidateWireBytes &&
          decodeConfigurationRequest(configurationRequest, configurationRequestBytes,
              &decodedRequest, &decodedCandidate) && decodedRequest.requestId == 88u &&
          decodedCandidate.expectedGeneration == candidate.expectedGeneration &&
          decodedCandidate.staticIPv4.address == candidate.staticIPv4.address &&
          sameInterfaceIdentity(decodedCandidate.stableId, candidate.stableId,
              kInterfaceIdBytes),
          "fixed candidate request round-trips generation, identity, and complete IPv4 state");
    uint8_t malformedConfiguration[kMaxRequestBytes]{};
    std::memcpy(malformedConfiguration, configurationRequest, configurationRequestBytes);
    malformedConfiguration[kRequestHeaderBytes + 46] = 1u;
    check(!decodeConfigurationRequest(malformedConfiguration,
              configurationRequestBytes, &decodedRequest, &decodedCandidate),
          "reserved mutation payload bytes must be zero");
    NetworkConfigurationCandidate badCandidate = candidate;
    badCandidate.staticIPv4.gateway = 0x0A010801u;
    check(!encodeConfigurationRequest(badCandidate, 89u, configurationRequest,
              sizeof(configurationRequest), &configurationRequestBytes),
          "malformed candidate cannot be encoded by the production client");
    check(encodeConfigurationRequest(candidate, 88u, configurationRequest,
              sizeof(configurationRequest), &configurationRequestBytes),
          "valid request remains available after a rejected local candidate");

    ConfigurationTransactionResult transactionResult{};
    transactionResult.outcome = TransactionOutcome::VerifyFailedRollbackFailed;
    transactionResult.field = ConfigurationField::Gateway;
    transactionResult.resultingGeneration = 13u;
    uint8_t transactionResultWire[kConfigurationResultWireBytes]{};
    ConfigurationTransactionResult decodedTransactionResult{};
    check(encodeConfigurationResult(transactionResult, transactionResultWire,
              sizeof(transactionResultWire)) &&
          decodeConfigurationResult(transactionResultWire, sizeof(transactionResultWire),
              &decodedTransactionResult) &&
          decodedTransactionResult.outcome == transactionResult.outcome &&
          decodedTransactionResult.field == transactionResult.field &&
          decodedTransactionResult.resultingGeneration == 13u,
          "transaction outcome, rollback status, and resulting generation round-trip");

    NetworkSnapshot source = makeKernelSnapshot(12u, 0x0A00020Fu);
    uint8_t snapshotWire[kSnapshotWireBytes]{};
    NetworkSnapshot roundTrip{};
    check(encodeSnapshot(source, snapshotWire, sizeof(snapshotWire)) &&
          decodeSnapshot(snapshotWire, sizeof(snapshotWire), &roundTrip) &&
          sameSnapshot(source, roundTrip),
          "fixed-capacity snapshot encodes and decodes field-for-field");
    uint8_t malformedSnapshot[kSnapshotWireBytes]{};
    std::memcpy(malformedSnapshot, snapshotWire, sizeof(snapshotWire));
    writeU32(malformedSnapshot + 16, static_cast<uint32_t>(kMaxAdapters + 1));
    check(!decodeSnapshot(malformedSnapshot, sizeof(malformedSnapshot), &roundTrip),
          "snapshot capacity overflow is rejected at decode");
    check(!decodeSnapshot(snapshotWire, sizeof(snapshotWire) - 1, &roundTrip),
          "truncated snapshot body is rejected");
    std::memcpy(malformedSnapshot, snapshotWire, sizeof(snapshotWire));
    malformedSnapshot[kSnapshotHeaderBytes + 116] = 0xFFu;
    check(!decodeSnapshot(malformedSnapshot, sizeof(malformedSnapshot), &roundTrip),
          "unknown link-state values are rejected");
    std::memcpy(malformedSnapshot, snapshotWire, sizeof(snapshotWire));
    for (size_t i = 0; i < kNameBytes; ++i)
        malformedSnapshot[kSnapshotHeaderBytes + 36 + i] = static_cast<uint8_t>('x');
    check(!decodeSnapshot(malformedSnapshot, sizeof(malformedSnapshot), &roundTrip),
          "unterminated fixed-capacity text fields are rejected");
    std::memcpy(malformedSnapshot, snapshotWire, sizeof(snapshotWire));
    malformedSnapshot[kSnapshotHeaderBytes + 124] = 2u;
    check(!decodeSnapshot(malformedSnapshot, sizeof(malformedSnapshot), &roundTrip),
          "non-boolean address availability is rejected");
    std::memcpy(malformedSnapshot, snapshotWire, sizeof(snapshotWire));
    malformedSnapshot[kSnapshotHeaderBytes + kAdapterWireBytes] = 1u;
    check(!decodeSnapshot(malformedSnapshot, sizeof(malformedSnapshot), &roundTrip),
          "unused adapter rows must remain zero-filled");
    NetworkSnapshot generationless = source;
    generationless.generation = 0;
    check(!encodeSnapshot(generationless, snapshotWire, sizeof(snapshotWire)),
          "authoritative response requires a nonzero generation");

    ProviderState providerState;
    providerState.snapshot = source;
    Provider provider{ &providerState, readSnapshot };
    uint8_t response[kMaxResponseBytes]{};
    size_t responseBytes = 0;
    check(dispatch(request, requestBytes, provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes),
          "valid bounded request dispatches through the explicit service provider");
    ResponseHeader responseHeader{};
    check(decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::Ok &&
          responseHeader.requestId == 77u && responseHeader.payloadBytes == kSnapshotWireBytes,
          "valid response carries the matching id and fixed snapshot size");
    check(dispatch(request, requestBytes, provider, DispatchTrust::Untrusted,
              response, sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::Unauthorized && providerState.calls == 1,
          "untrusted endpoint is denied before the provider is reached");

    uint8_t badVersionRequest[kMaxRequestBytes]{};
    std::memcpy(badVersionRequest, request, sizeof(request));
    writeU16(badVersionRequest + 4, static_cast<uint16_t>(kProtocolVersion + 1));
    check(dispatch(badVersionRequest, requestBytes, provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::BadVersion,
          "protocol version mismatch fails cleanly");
    uint8_t unknownRequest[kMaxRequestBytes]{};
    std::memcpy(unknownRequest, request, sizeof(request));
    writeU16(unknownRequest + 6, 0x7FFFu);
    check(dispatch(unknownRequest, requestBytes, provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::Unsupported,
          "unknown request type fails as unsupported without arbitrary dispatch");
    uint8_t badLengthRequest[kMaxRequestBytes]{};
    std::memcpy(badLengthRequest, request, sizeof(request));
    writeU32(badLengthRequest + 12, 1u);
    check(dispatch(badLengthRequest, sizeof(badLengthRequest), provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::InvalidRequest,
          "nonzero request payload length is rejected");

    const uint32_t providerCallsBeforeMutation = providerState.calls;
    const bool mutationDispatched = dispatch(configurationRequest,
        configurationRequestBytes, provider, DispatchTrust::TrustedSystemServicePeer,
        response, sizeof(response), &responseBytes);
    const bool mutationHeaderDecoded = mutationDispatched &&
        decodeResponseHeader(response, responseBytes, &responseHeader);
    check(mutationHeaderDecoded && responseHeader.type == 0x8002u &&
          responseHeader.requestId == 88u,
          "mutation request receives a typed response with its matching request id");
    check(mutationHeaderDecoded && responseHeader.status == ResponseStatus::Unauthorized &&
          providerState.calls == providerCallsBeforeMutation,
          "valid Settings mutation is denied because COM2 peer authentication is absent");
    NetworkConfigurationCandidate staleWireCandidate = candidate;
    staleWireCandidate.expectedGeneration = 11u;
    size_t staleRequestBytes = 0;
    uint8_t staleConfigurationRequest[kMaxRequestBytes]{};
    check(encodeConfigurationRequest(staleWireCandidate, 90u,
              staleConfigurationRequest, sizeof(staleConfigurationRequest),
              &staleRequestBytes) &&
          dispatch(staleConfigurationRequest, staleRequestBytes, provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::Unauthorized &&
          providerState.calls == providerCallsBeforeMutation,
          "stale candidate is denied at the bridge without reaching network owners");
    check(dispatch(malformedConfiguration, configurationRequestBytes, provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::InvalidRequest &&
          providerState.calls == providerCallsBeforeMutation,
          "malformed mutation request is rejected before any network provider call");
    uint8_t oversizedRequest[kMaxRequestBytes + 1]{};
    std::memcpy(oversizedRequest, configurationRequest, configurationRequestBytes);
    check(dispatch(oversizedRequest, sizeof(oversizedRequest), provider,
              DispatchTrust::TrustedSystemServicePeer, response,
              sizeof(response), &responseBytes) &&
          decodeResponseHeader(response, responseBytes, &responseHeader) &&
          responseHeader.status == ResponseStatus::InvalidRequest,
          "request above the fixed protocol maximum is rejected");

    TransportState transportState;
    transportState.provider = provider;
    Transport transport{ &transportState, fakeTransact };
    SystemServiceClient client(transport, 150u);
    NetworkSnapshot received{};
    check(client.getNetworkSnapshot(&received) == ClientResult::Ok &&
          received.generation == 12u && received.backend == Backend::Kernel &&
          received.adapters[0].ipv4Address.value == 0x0A00020Fu,
          "client receives authoritative provider state and generation");
    check(transportState.transactions == 1 && providerState.calls == 2,
          "client performs one bounded request through the dispatcher");

    ConfigurationTransactionResult returnedTransaction{};
    const uint32_t beforeDeniedMutationCalls = providerState.calls;
    check(client.setNetworkConfiguration(candidate, &returnedTransaction) == ClientResult::Unauthorized &&
          providerState.calls == beforeDeniedMutationCalls,
          "production mutation client receives the bridge authorization denial without provider mutation");

    providerState.snapshot = makeKernelSnapshot(13u, 0x0A000210u);
    check(client.getNetworkSnapshot(&received) == ClientResult::Ok &&
          received.generation == 13u && received.adapters[0].ipv4Address.value == 0x0A000210u,
          "repeated request receives the updated generation and snapshot");
    NetworkInterfaceInfo selected{};
    check(!adapterForGeneration(received, source.adapters[0].interfaceId, 12u, &selected),
          "a prior generation cannot select an interface from a newer snapshot");

    transportState.fault = TransportFault::Timeout;
    check(client.getNetworkSnapshot(&received) == ClientResult::Timeout &&
          received.backend == Backend::Unavailable && received.generation == 0u,
          "timeout clears the prior snapshot and returns unavailable");
    check(client.setNetworkConfiguration(candidate, &returnedTransaction) == ClientResult::Timeout,
          "mutation timeout is returned distinctly and never reported as success");
    transportState.fault = TransportFault::None;
    providerState.snapshot = makeKernelSnapshot(1u, 0x0A000211u);
    check(client.getNetworkSnapshot(&received) == ClientResult::Ok &&
          received.generation == 1u && received.adapters[0].ipv4Address.value == 0x0A000211u,
          "service restart supplies a fresh low generation after unavailable state");
    transportState.fault = TransportFault::Disconnected;
    check(client.getNetworkSnapshot(&received) == ClientResult::Disconnected &&
          received.state == SnapshotState::Unavailable,
          "disconnect is bounded and clears cached live state");
    check(client.setNetworkConfiguration(candidate, &returnedTransaction) == ClientResult::Disconnected,
          "disconnect during mutation is returned distinctly");
    check(client.getNetworkSnapshot(&received) == ClientResult::Disconnected &&
          received.backend == Backend::Unavailable,
          "repeated disconnects remain deterministic and do not reuse stale state");
    transportState.fault = TransportFault::Truncated;
    check(client.getNetworkSnapshot(&received) == ClientResult::ProtocolError,
          "truncated response is rejected");
    transportState.fault = TransportFault::BadResponseVersion;
    check(client.getNetworkSnapshot(&received) == ClientResult::ProtocolError,
          "bad response protocol version is rejected");
    transportState.fault = TransportFault::WrongRequestId;
    check(client.getNetworkSnapshot(&received) == ClientResult::ProtocolError,
          "mismatched response request id is rejected");
    transportState.fault = TransportFault::Oversized;
    check(client.getNetworkSnapshot(&received) == ClientResult::ProtocolError,
          "oversized response is rejected");

    transportState.fault = TransportFault::ClosePending;
    const auto closeStart = std::chrono::steady_clock::now();
    check(client.getNetworkSnapshot(&received) == ClientResult::Timeout &&
          transportState.closeRequested && transportState.lastTimeoutMs == 150u &&
          received.backend == Backend::Unavailable,
          "Settings close during an outstanding request resolves through the client deadline");
    check(std::chrono::steady_clock::now() - closeStart < std::chrono::milliseconds(100),
          "bounded cancellation simulation returns promptly");

    transportState.fault = TransportFault::None;
    providerState.result = Result::Unavailable;
    providerState.snapshot = NetworkSnapshot{};
    providerState.snapshot.backend = Backend::Kernel;
    providerState.snapshot.state = SnapshotState::Unavailable;
    providerState.snapshot.generation = 14u;
    check(client.getNetworkSnapshot(&received) == ClientResult::Unavailable &&
          received.backend == Backend::Kernel && received.generation == 14u,
          "provider unavailability remains distinct from a missing service");
    transportState.provider = Provider{};
    check(client.getNetworkSnapshot(&received) == ClientResult::Unavailable &&
          received.backend == Backend::Unavailable && received.generation == 0u,
          "absent provider returns a truthful unavailable snapshot");

    providerState.result = Result::Ok;
    providerState.snapshot = source;
    providerState.snapshot.adapterCount = static_cast<uint32_t>(kMaxAdapters + 1);
    transportState.provider = provider;
    check(client.getNetworkSnapshot(&received) == ClientResult::ProtocolError,
          "malformed provider capacity is rejected without clamping across the boundary");

    check(gxos::apps::settings::isAuthorizedSettingsProcessIdentity(
              true, "settings", "gxos.builtin.settings"),
          "trusted hosted Settings metadata authorizes read-only network service access");
    check(!gxos::apps::settings::isAuthorizedSettingsProcessIdentity(
              true, "ordinary-app", "gxos.builtin.settings") &&
          !gxos::apps::settings::isAuthorizedSettingsProcessIdentity(
              true, "settings", "ordinary.app") &&
          !gxos::apps::settings::isAuthorizedSettingsProcessIdentity(
              false, "settings", "gxos.builtin.settings"),
          "ordinary, mismatched, and missing runtime identities are denied");
    check(gxos::apps::settings::mapSystemServiceResult(ClientResult::Timeout) == Result::Unavailable &&
          gxos::apps::settings::mapSystemServiceResult(ClientResult::Disconnected) == Result::Unavailable,
          "Settings service fallback maps transport failures to unavailable");
    SystemServiceClient absentClient(Transport{}, 100u);
    check(absentClient.getNetworkSnapshot(&received) == ClientResult::Unavailable &&
          received.backend == Backend::Unavailable && received.generation == 0u,
          "a missing system-service transport produces an unavailable snapshot");

    const NetworkSnapshot beforeTelemetry = source;
    gxos::net::armNetworkTelemetry("hostedSocketCounters");
    gxos::net::recordNetworkBytesSent(4096u);
    const gxos::net::NetworkTelemetrySnapshot telemetry = gxos::net::networkTelemetrySnapshot();
    check(telemetry.available && telemetry.bytesSentTotal >= 4096u &&
          source.backend == beforeTelemetry.backend && source.generation == beforeTelemetry.generation &&
          source.adapters[0].ipv4Address.value == beforeTelemetry.adapters[0].ipv4Address.value,
          "hosted socket telemetry stays separate from authoritative kernel snapshots");

    std::cout << "System-service bridge tests: " << (checks - failures) << "/" << checks << " passed\n";
    return failures == 0 ? 0 : 1;
}
