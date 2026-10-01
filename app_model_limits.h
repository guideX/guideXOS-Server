#pragma once

#include <cstddef>

namespace gxos { namespace apps {

// Shared bounds for identities and values retained by the App Model.
constexpr size_t kAppModelMaxAppIdBytes = 128;
constexpr size_t kAppModelMaxDisplayNameBytes = 128;
constexpr size_t kAppModelMaxLaunchTargetBytes = 4096;
constexpr size_t kAppModelMaxEntryPathBytes = 4096;
constexpr size_t kAppModelMaxEntriesPerManifest = 64;
constexpr size_t kAppModelMaxRegistryApps = 512;
constexpr size_t kAppModelMaxManifestBytes = 1024 * 1024;

} }
