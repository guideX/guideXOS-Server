#ifndef KERNEL_SETTINGS_INVENTORY_PROVIDER_H
#define KERNEL_SETTINGS_INVENTORY_PROVIDER_H

#include "system_service_dispatcher.h"

namespace kernel {
namespace settings_inventory_provider {

gxos::system_service::ResponseStatus readDeviceSnapshot(
    void* context, gxos::settings_inventory::DeviceSnapshot* output);
gxos::system_service::ResponseStatus readStorageSnapshot(
    void* context, gxos::settings_inventory::StorageSnapshot* output);

gxos::system_service::DispatchProviders appModelProvider();

} // namespace settings_inventory_provider
} // namespace kernel

#endif
