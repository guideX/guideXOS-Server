#pragma once

#include <cstddef>

namespace gxos { namespace apps {

// Shared bounds for identities and values retained by the App Model.
constexpr size_t kAppModelMaxAppIdBytes = 128;
constexpr size_t kAppModelMaxDisplayNameBytes = 128;
constexpr size_t kAppModelMaxLaunchTargetBytes = 4096;
constexpr size_t kAppModelMaxEntryPathBytes = 4096;
constexpr size_t kAppModelMaxDocumentPathBytes = 4096;
constexpr size_t kAppModelMaxFolderPathBytes = kAppModelMaxDocumentPathBytes;
// Keep activation payloads aligned with guideWeb's bounded URL parser.
constexpr size_t kAppModelMaxUriBytes = 2048;
constexpr size_t kAppModelMaxFileExtensionBytes = 32;
constexpr size_t kAppModelMaxFileAssociationsPerApp = 16;
constexpr size_t kAppModelMaxFileAssociationRecords = 256;
constexpr size_t kAppModelMaxProtocolSchemeBytes = 32;
constexpr size_t kAppModelMaxProtocolsPerApp = 8;
constexpr size_t kAppModelMaxProtocolRecords = 256;
// Open With snapshots stay small and fixed-capacity. The current default is
// always promoted into the retained prefix before truncation is reported.
constexpr size_t kAppModelMaxDocumentHandlersPerExtension = 16;
constexpr size_t kAppModelMaxProtocolHandlersPerScheme = 16;
constexpr size_t kAppModelMaxFolderHandlerRecords = 256;
constexpr size_t kAppModelMaxFolderHandlers = 16;
// App actions are small manifest-owned declarations. Enumeration is fixed at
// the per-app bound; the registry-wide theoretical maximum is the product of
// the app and per-app declaration limits.
constexpr size_t kAppModelMaxActionIdBytes = 48;
constexpr size_t kAppModelMaxActionLabelBytes = 64;
constexpr size_t kAppModelMaxActionsPerApp = 12;
constexpr size_t kAppModelMaxEntriesPerManifest = 64;
constexpr size_t kAppModelMaxRegistryApps = 512;
constexpr size_t kAppModelMaxActionRecords = kAppModelMaxRegistryApps * kAppModelMaxActionsPerApp;
constexpr size_t kAppModelMaxManifestBytes = 1024 * 1024;

} }
