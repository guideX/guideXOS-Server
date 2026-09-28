#pragma once

#include "allocator.h"
#include "built_in_app_metadata.h"
#include "network_settings_contract.h"
#include "process.h"
#include "system_service_client.h"
#include "system_service_qemu_transport.h"

#include <string>

namespace gxos {
namespace apps {
namespace settings {

// Retained as an explicit failure provider for callers that need to describe
// a missing bridge. It never substitutes host operating-system NIC state.
inline network_settings::Result hostedGuideXosNetworkSnapshot(
    void*, network_settings::NetworkSnapshot* output)
{
    if (!output) return network_settings::Result::InvalidArgument;
    *output = network_settings::NetworkSnapshot{};
    output->backend = network_settings::Backend::Unavailable;
    output->state = network_settings::SnapshotState::Unavailable;
    return network_settings::Result::Unavailable;
}

inline network_settings::Provider hostedGuideXosNetworkProvider()
{
    return network_settings::Provider{ nullptr, hostedGuideXosNetworkSnapshot };
}

inline bool isAuthorizedSettingsProcessIdentity(bool identityAvailable,
                                                const std::string& processName,
                                                const std::string& appId)
{
    if (!identityAvailable || processName != "settings" ||
        appId != "gxos.builtin.settings") return false;
    const apps::BuiltInAppMetadata* metadata =
        apps::FindBuiltInAppMetadataByAppId(appId.c_str());
    return metadata && metadata->appId &&
        std::string(metadata->appId) == appId &&
        apps::IsBuiltInAppAvailableInHosted(*metadata);
}

// The active process-table record is assigned by SettingsCenter::Launch. The
// appId is never accepted from the service request or from a Settings UI
// argument, so an ordinary app cannot self-assert the built-in Settings role.
inline bool isAuthorizedSettingsProcess()
{
    const uint64_t pid = Allocator::currentPid();
    if (pid == 0) return false;
    std::string processName;
    std::string appId;
    return ProcessTable::getIdentity(pid, processName, appId) &&
        isAuthorizedSettingsProcessIdentity(true, processName, appId);
}

inline network_settings::Result mapSystemServiceResult(system_service::ClientResult result)
{
    using system_service::ClientResult;
    using network_settings::Result;
    switch (result) {
    case ClientResult::Ok: return Result::Ok;
    case ClientResult::Unauthorized: return Result::Unauthorized;
    case ClientResult::InvalidArgument: return Result::InvalidArgument;
    case ClientResult::Unsupported: return Result::Unsupported;
    case ClientResult::Unavailable:
    case ClientResult::Timeout:
    case ClientResult::Disconnected:
    case ClientResult::ProtocolError:
    case ClientResult::Failed:
    default: return Result::Unavailable;
    }
}

inline network_settings::Result readSettingsNetworkSnapshot(
    const system_service::Transport& transport,
    network_settings::NetworkSnapshot* output)
{
    using namespace network_settings;
    if (!output) return Result::InvalidArgument;
    *output = NetworkSnapshot{};
    output->backend = Backend::Unavailable;
    output->state = SnapshotState::Unavailable;
    if (!isAuthorizedSettingsProcess()) return Result::Unauthorized;
    system_service::SystemServiceClient client(transport);
    return mapSystemServiceResult(client.getNetworkSnapshot(output));
}

inline network_settings::Result readSettingsNetworkSnapshot(
    network_settings::NetworkSnapshot* output)
{
    static system_service::QemuCom2TcpTransport transport;
    return readSettingsNetworkSnapshot(transport.asTransport(), output);
}

inline network_settings::Result applySettingsNetworkConfiguration(
    const system_service::Transport& transport,
    const network_settings::NetworkConfigurationCandidate& candidate,
    network_settings::ConfigurationTransactionResult* transactionOutput)
{
    using namespace network_settings;
    if (transactionOutput) *transactionOutput = ConfigurationTransactionResult{};
    if (!isAuthorizedSettingsProcess()) return Result::Unauthorized;
    if (validateCandidate(candidate) != ConfigurationField::None)
        return Result::InvalidArgument;
    system_service::SystemServiceClient client(transport);
    return mapSystemServiceResult(
        client.setNetworkConfiguration(candidate, transactionOutput));
}

inline network_settings::Result applySettingsNetworkConfiguration(
    const network_settings::NetworkConfigurationCandidate& candidate,
    network_settings::ConfigurationTransactionResult* transactionOutput)
{
    static system_service::QemuCom2TcpTransport transport;
    return applySettingsNetworkConfiguration(transport.asTransport(), candidate,
        transactionOutput);
}

inline network_settings::Result readSettingsNetworkSnapshot(
    const network_settings::Provider& provider,
    network_settings::NetworkSnapshot* output)
{
    return network_settings::readSnapshot(provider,
        network_settings::Client::BuiltInSettings, output);
}

} // namespace settings
} // namespace apps
} // namespace gxos
