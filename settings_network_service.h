#pragma once

#include "network_settings_contract.h"

namespace gxos {
namespace apps {
namespace settings {

// Hosted guideXOS Server and bare-metal guideXOS are separate runtime
// environments. This provider intentionally does not inspect Windows/Linux
// adapters or infer kernel state from hosted socket telemetry.
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
