#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gxos {
namespace apps {

// The shared built-in table is the guideXOS App Model record source used by
// both the hosted registry and the freestanding kernel resolver.  A managed
// NativeAOT record describes identity only; it does not carry a function
// pointer, managed address, TLS value, or other runtime implementation detail.
enum class BuiltInAppLaunchKind {
	Native = 0,
	ManagedNativeAot
};

static constexpr const char* kManagedNativeAotCompositeImagePath =
	"/system/apps/GXOSAPP.ELF";

// Shared built-in app metadata for both hosted synthetic manifest registration
// and bare-metal kernel registration. This table is metadata-only for now; it
// does not replace existing launch dispatch or factory wiring yet.
enum BuiltInAppAvailability {
	BuiltInAvailabilityNone = 0,
	BuiltInAvailabilityHosted = 1u << 0,
	BuiltInAvailabilityBareMetal = 1u << 1
};

struct BuiltInAppMetadata {
	const char* appId;
	const char* displayName;
	const char* launchName;
	const char* kernelAppName;
	const char* kernelLegacyAlias;
	const char* iconKey;
	const char* category;
	const char* description;
	unsigned int availability;
	unsigned int kernelIconColor;
	int defaultWindowWidth;
	int defaultWindowHeight;
	bool defaultWindowResizable;
	bool canTombstone = false; // App capability policy only; diagnostic tombstones stay separate from restore support.
	const char* const* knownAliases = nullptr;
	size_t knownAliasCount = 0;
	bool appearsInStartMenu = false;
	bool canAppearOnDesktop = false;
	bool recordRecentPrograms = false;
	bool acceptsFileTargets = false;
	bool acceptsFolderTargets = false;
	bool systemShellObject = false;
	bool riskyForActiveTypedDispatch = false;
	BuiltInAppLaunchKind launchKind = BuiltInAppLaunchKind::Native;
	uint32_t managedSelector = 0;
	const char* managedCompositeImagePath = nullptr;
};

namespace detail {
inline bool builtInTextEquals(const char* a, const char* b) {
	if (a == b) return true;
	if (!a || !b) return false;
	while (*a && *b) {
		if (*a != *b) return false;
		++a;
		++b;
	}
	return *a == '\0' && *b == '\0';
}
} // namespace detail

namespace detail {
static const char* const kConsoleAliases[] = {
	"Terminal"
};

static const char* const kFileExplorerAliases[] = {
	"Files",
	"FileExplorer"
};

static const char* const kImageViewerAliases[] = {
	"Image Viewer",
	"ImgViewer"
};

static const char* const kControlPanelAliases[] = {
	"Control Panel",
	"Settings"
};

static const char* const kDisplayOptionsAliases[] = {
	"System Settings",
	"Display Options",
	"Display Settings",
	"Desktop Background",
	"Wallpaper"
};

static const char* const kAppModelAliases[] = {
	"AppModel"
};

static const char* const kManagedWorkspaceAliases[] = {
	// C109 compatibility aliases are intentionally not user-facing records.
	"com.guidexos.nativeaot.hostlogproof.app-a"
};

static const char* const kManagedStatusAliases[] = {
	// C109 compatibility aliases are intentionally not user-facing records.
	"com.guidexos.nativeaot.hostlogproof.app-b"
};

static const char* const kManagedCounterAliases[] = {
	"Counter"
};

static const char* const kManagedNotesAliases[] = {
	"Notes"
};

#if defined(GXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW)
static const char* const kManagedScrollViewAliases[] = {
	"ScrollView"
};
#endif

#if defined(GXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK)
static const char* const kManagedVerticalStackAliases[] = {
	"Vertical Stack",
	"Stack"
};
#endif

static const char* const kOnScreenKeyboardAliases[] = {
	"On Screen Keyboard"
};

static const char* const kShutdownDialogAliases[] = {
	"Shutdown Dialog"
};

} // namespace detail

static const BuiltInAppMetadata kBuiltInAppMetadata[] = {
	{ "gxos.builtin.notepad", "Notepad", "Notepad", "Notepad", nullptr, "app.notepad", "Accessories", "Built-in guideXOS text editor.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF78B450u, 0, 0, false, false, nullptr, 0, true, true, true, true, false, false, false },
	{ "gxos.builtin.calculator", "Calculator", "Calculator", "Calculator", nullptr, "app.calculator", "Utilities", "Built-in guideXOS calculator.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF4690C8u, 0, 0, false, false, nullptr, 0, true, true, true, false, false, false, false },
	{ "gxos.builtin.clock", "Clock", "Clock", "Clock", nullptr, "app.clock", "Utilities", "Built-in guideXOS clock.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF4A88D8u, 0, 0, false, false, nullptr, 0, true, true, true, false, false, false, false },
	{ "gxos.builtin.console", "Console", "Console", nullptr, nullptr, "app.console", "System", "Built-in guideXOS console window.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, detail::kConsoleAliases, sizeof(detail::kConsoleAliases) / sizeof(detail::kConsoleAliases[0]), true, true, true, false, false, true, false },
	{ "gxos.builtin.fileexplorer", "File Explorer", "FileExplorer", "Files", "Files", "app.files", "System", "Built-in guideXOS file explorer.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFFC8B43Cu, 0, 0, false, false, detail::kFileExplorerAliases, sizeof(detail::kFileExplorerAliases) / sizeof(detail::kFileExplorerAliases[0]), true, true, true, false, true, false, false },
	{ "gxos.builtin.trash", "Trash", "Trash", "Trash", nullptr, "trash.empty", "System", "Built-in guideXOS Trash placeholder.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF9098A4u, 0, 0, false, false, nullptr, 0, true, true, true, false, false, true, false },
	{ "gxos.builtin.taskmanager", "TaskManager", "TaskManager", "TaskManager", nullptr, "app.taskmanager", "System", "Built-in guideXOS task manager.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFFB44646u, 0, 0, false, false, nullptr, 0, true, true, true, false, false, false, false },
	{ "gxos.builtin.paint", "Paint", "Paint", nullptr, nullptr, "app.paint", "Graphics", "Built-in guideXOS paint application.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, nullptr, 0, true, true, true, false, false, false, false },
	// Hosted keeps the full editor/viewer path; bare-metal wires to the minimal
	// PNG viewer app and legacy ImgViewer alias below.
	{ "gxos.builtin.imageviewer", "Image Viewer", "ImageViewer", "ImageViewer", "ImgViewer", "app.generic", "Graphics", "Built-in guideXOS image viewer.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFFC87830u, 820, 620, true, false, detail::kImageViewerAliases, sizeof(detail::kImageViewerAliases) / sizeof(detail::kImageViewerAliases[0]), true, true, true, true, false, false, false },
	{ "gxos.builtin.onscreenkeyboard", "OnScreenKeyboard", "OnScreenKeyboard", nullptr, nullptr, "app.generic", "Accessibility", "Built-in guideXOS on-screen keyboard.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, detail::kOnScreenKeyboardAliases, sizeof(detail::kOnScreenKeyboardAliases) / sizeof(detail::kOnScreenKeyboardAliases[0]), false, false, false, false, false, true, true },
	{ "gxos.builtin.shutdowndialog", "ShutdownDialog", "ShutdownDialog", nullptr, nullptr, "app.generic", "System", "Built-in guideXOS shutdown dialog.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, detail::kShutdownDialogAliases, sizeof(detail::kShutdownDialogAliases) / sizeof(detail::kShutdownDialogAliases[0]), false, false, false, false, false, true, true },
	{ "gxos.builtin.diskmanager", "DiskManager", "DiskManager", "DiskManager", nullptr, "app.diskmanager", "System", "Built-in guideXOS disk manager.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF7050C0u, 0, 0, false, false, nullptr, 0, true, true, true, false, false, false, false },
	{ "gxos.builtin.controlpanel", "ControlPanel", "ControlPanel", nullptr, nullptr, "app.controlpanel", "System", "Built-in guideXOS control panel.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, detail::kControlPanelAliases, sizeof(detail::kControlPanelAliases) / sizeof(detail::kControlPanelAliases[0]), true, true, true, false, false, true, false },
	{ "gxos.builtin.displayoptions", "DisplayOptions", "DisplayOptions", "DisplayOptions", nullptr, "app.settings", "System", "Built-in guideXOS display options.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF606878u, 0, 0, false, false, detail::kDisplayOptionsAliases, sizeof(detail::kDisplayOptionsAliases) / sizeof(detail::kDisplayOptionsAliases[0]), true, true, true, false, false, true, false },
	{ "guidexos.navigator", "guideXOS Navigator", "guideXOS Navigator", "guideXOS Navigator", nullptr, "app.navigator", "Internet", "Native guideXOS Navigator browser bundled with the OS app model.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF4678BEu, 920, 640, false, false, nullptr, 0, true, true, true, false, false, false, false },
	{ "gxos.builtin.appmodeldemo", "App Model Demo", "App Model Demo", nullptr, nullptr, "app.generic", "Diagnostics", "Built-in guideXOS app-model diagnostics viewer.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, detail::kAppModelAliases, sizeof(detail::kAppModelAliases) / sizeof(detail::kAppModelAliases[0]), true, true, true, false, false, false, false },
	{ "gxos.builtin.nativeappdebugviewer", "Native App Debug Viewer", "Native App Debug Viewer", nullptr, nullptr, "app.generic", "Diagnostics", "Built-in guideXOS native app diagnostics viewer.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, nullptr, 0, false, false, false, false, false, true, true },
	{ "gxos.builtin.hdinstaller", "HDInstaller", "HDInstaller", nullptr, nullptr, "app.installer", "Installer", "Built-in guideXOS installer entry for supported runtime targets.", BuiltInAvailabilityHosted, 0, 0, 0, false, false, nullptr, 0, true, false, false, false, false, true, true },
	{ "com.guidexos.apps.managed.workspace", "Managed Workspace", "Managed Workspace", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS application hosted in the resident NativeAOT composite image.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedWorkspaceAliases, sizeof(detail::kManagedWorkspaceAliases) / sizeof(detail::kManagedWorkspaceAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 1u, kManagedNativeAotCompositeImagePath },
	{ "com.guidexos.apps.managed.status", "Managed Status", "Managed Status", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS status application hosted in the resident NativeAOT composite image.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedStatusAliases, sizeof(detail::kManagedStatusAliases) / sizeof(detail::kManagedStatusAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 2u, kManagedNativeAotCompositeImagePath },
	{ "com.guidexos.apps.managed.counter", "Managed Counter", "Managed Counter", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS counter application hosted in the resident NativeAOT composite image.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedCounterAliases, sizeof(detail::kManagedCounterAliases) / sizeof(detail::kManagedCounterAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 3u, kManagedNativeAotCompositeImagePath },
	{ "com.guidexos.apps.managed.notes", "Managed Notes", "Managed Notes", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS notes application using the bounded managed file service.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedNotesAliases, sizeof(detail::kManagedNotesAliases) / sizeof(detail::kManagedNotesAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 4u, kManagedNativeAotCompositeImagePath }
#if defined(GXOS_NATIVEAOT_C140_MANAGED_SCROLL_VIEW)
,	{ "com.guidexos.apps.managed.scrollview", "Managed ScrollView", "Managed ScrollView", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS bounded vertical ScrollView proof application.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedScrollViewAliases, sizeof(detail::kManagedScrollViewAliases) / sizeof(detail::kManagedScrollViewAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 5u, kManagedNativeAotCompositeImagePath }
#endif
#if defined(GXOS_NATIVEAOT_C141_MANAGED_VERTICAL_STACK)
,	{ "com.guidexos.apps.managed.verticalstack", "Managed Vertical Stack", "Managed Vertical Stack", nullptr, nullptr, "app.generic", "Utilities", "Managed guideXOS bounded non-owning vertical stack proof application.", BuiltInAvailabilityBareMetal, 0, 0, 0, false, false, detail::kManagedVerticalStackAliases, sizeof(detail::kManagedVerticalStackAliases) / sizeof(detail::kManagedVerticalStackAliases[0]), true, true, true, false, false, false, false, BuiltInAppLaunchKind::ManagedNativeAot, 5u, kManagedNativeAotCompositeImagePath }
#endif
};

static const int kBuiltInAppMetadataCount = sizeof(kBuiltInAppMetadata) / sizeof(kBuiltInAppMetadata[0]);

inline bool IsBuiltInAppAvailableInHosted(const BuiltInAppMetadata& metadata) {
	return (metadata.availability & BuiltInAvailabilityHosted) != 0;
}

inline bool IsBuiltInAppAvailableInBareMetal(const BuiltInAppMetadata& metadata) {
	return (metadata.availability & BuiltInAvailabilityBareMetal) != 0;
}

inline bool CanBuiltInAppTombstone(const BuiltInAppMetadata& metadata) {
	return metadata.canTombstone;
}

inline const char* BuiltInAppCanonicalLaunchName(const BuiltInAppMetadata& metadata) {
	if (metadata.launchName && metadata.launchName[0]) return metadata.launchName;
	if (metadata.displayName && metadata.displayName[0]) return metadata.displayName;
	return "";
}

inline bool BuiltInAppHasKnownAlias(const BuiltInAppMetadata& metadata, const char* alias) {
	if (!alias || !alias[0] || !metadata.knownAliases || metadata.knownAliasCount == 0) return false;
	for (size_t i = 0; i < metadata.knownAliasCount; ++i) {
		if (detail::builtInTextEquals(metadata.knownAliases[i], alias)) return true;
	}
	return false;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByDisplayName(const char* displayName) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (detail::builtInTextEquals(kBuiltInAppMetadata[i].displayName, displayName)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByAppId(const char* appId) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (detail::builtInTextEquals(kBuiltInAppMetadata[i].appId, appId)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByLaunchName(const char* launchName) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (detail::builtInTextEquals(kBuiltInAppMetadata[i].launchName, launchName)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByKernelAppName(const char* kernelAppName) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (detail::builtInTextEquals(kBuiltInAppMetadata[i].kernelAppName, kernelAppName)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByKernelLegacyAlias(const char* kernelLegacyAlias) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (detail::builtInTextEquals(kBuiltInAppMetadata[i].kernelLegacyAlias, kernelLegacyAlias)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByKnownAlias(const char* alias) {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		if (BuiltInAppHasKnownAlias(kBuiltInAppMetadata[i], alias)) return &kBuiltInAppMetadata[i];
	}
	return nullptr;
}

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByIdentity(const char* identity) {
	const BuiltInAppMetadata* metadata = FindBuiltInAppMetadataByAppId(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByDisplayName(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByLaunchName(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByKernelAppName(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByKernelLegacyAlias(identity);
	if (metadata) return metadata;
	return FindBuiltInAppMetadataByKnownAlias(identity);
}

inline bool IsManagedNativeAotApp(const BuiltInAppMetadata& metadata) {
	return metadata.launchKind == BuiltInAppLaunchKind::ManagedNativeAot;
}

inline bool IsManagedNativeAotRecordValid(const BuiltInAppMetadata& metadata) {
	if (IsManagedNativeAotApp(metadata)) {
		if (!metadata.appId || !metadata.appId[0] || metadata.managedSelector == 0 ||
			!metadata.managedCompositeImagePath ||
			!detail::builtInTextEquals(metadata.managedCompositeImagePath, kManagedNativeAotCompositeImagePath) ||
			(metadata.kernelAppName && metadata.kernelAppName[0])) {
			return false;
		}

		return true;
	}

	// A native record must not carry managed selector/image metadata by
	// accident.  This makes malformed test descriptors fail deterministically.
	return metadata.managedSelector == 0 &&
		(!metadata.managedCompositeImagePath || !metadata.managedCompositeImagePath[0]);
}

inline bool ManagedNativeAotCatalogIsValid() {
	for (size_t i = 0; i < kBuiltInAppMetadataCount; ++i) {
		const BuiltInAppMetadata& metadata = kBuiltInAppMetadata[i];
		if (!IsManagedNativeAotApp(metadata)) continue;
		if (!IsManagedNativeAotRecordValid(metadata)) return false;
		for (size_t j = i + 1; j < kBuiltInAppMetadataCount; ++j) {
			const BuiltInAppMetadata& other = kBuiltInAppMetadata[j];
			if (!IsManagedNativeAotApp(other)) continue;
			if ((metadata.appId && other.appId && detail::builtInTextEquals(metadata.appId, other.appId)) ||
				metadata.managedSelector == other.managedSelector) {
				return false;
			}
		}
	}
	return true;
}

inline const BuiltInAppMetadata* FindManagedNativeAotAppByIdentity(const char* identity) {
	const BuiltInAppMetadata* metadata = FindBuiltInAppMetadataByIdentity(identity);
	return metadata && IsManagedNativeAotApp(*metadata) ? metadata : nullptr;
}

} // namespace apps
} // namespace gxos
