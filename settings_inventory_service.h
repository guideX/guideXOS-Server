#pragma once

#include "settings_inventory_contract.h"
#include "settings_network_service.h"
#include "system_service_client.h"
#include "system_service_qemu_transport.h"

namespace gxos {
namespace apps {
namespace settings {

inline system_service::ClientResult readSettingsDeviceSnapshot(
    const system_service::Transport& transport,
    settings_inventory::DeviceSnapshot* output)
{
    using namespace settings_inventory;
    if (!output) return system_service::ClientResult::InvalidArgument;
    *output = DeviceSnapshot{};
    if (!isAuthorizedSettingsProcess()) return system_service::ClientResult::Unauthorized;
    system_service::SystemServiceClient client(transport);
    return client.getDeviceSnapshot(output);
}

inline system_service::ClientResult readSettingsStorageSnapshot(
    const system_service::Transport& transport,
    settings_inventory::StorageSnapshot* output)
{
    using namespace settings_inventory;
    if (!output) return system_service::ClientResult::InvalidArgument;
    *output = StorageSnapshot{};
    if (!isAuthorizedSettingsProcess()) return system_service::ClientResult::Unauthorized;
    system_service::SystemServiceClient client(transport);
    return client.getStorageSnapshot(output);
}

inline system_service::ClientResult readSettingsDeviceSnapshot(
    settings_inventory::DeviceSnapshot* output)
{
    static system_service::QemuCom2TcpTransport transport;
    return readSettingsDeviceSnapshot(transport.asTransport(), output);
}

inline system_service::ClientResult readSettingsStorageSnapshot(
    settings_inventory::StorageSnapshot* output)
{
    static system_service::QemuCom2TcpTransport transport;
    return readSettingsStorageSnapshot(transport.asTransport(), output);
}

} // namespace settings
} // namespace apps
} // namespace gxos
