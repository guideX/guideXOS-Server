#ifndef KERNEL_NETWORK_SETTINGS_PROVIDER_H
#define KERNEL_NETWORK_SETTINGS_PROVIDER_H

#include "../../../../network_settings_contract.h"

namespace kernel {
namespace network_settings_provider {

// Copies authoritative kernel NIC/DHCP/IPv4 state into the bounded App Model
// contract. The output contains no kernel-owned pointers.
gxos::network_settings::Result readSnapshot(
    gxos::network_settings::NetworkSnapshot* output);

// Adapts the kernel snapshot reader to the same bounded provider contract
// consumed by hosted Settings and deterministic test providers.
gxos::network_settings::Provider appModelProvider();

} // namespace network_settings_provider
} // namespace kernel

#endif
