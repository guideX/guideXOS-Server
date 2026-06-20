#pragma once

namespace gxos {
namespace apps {

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

static const BuiltInAppMetadata kBuiltInAppMetadata[] = {
	{ "gxos.builtin.notepad", "Notepad", "Notepad", "Notepad", nullptr, "app.notepad", "Accessories", "Built-in guideXOS text editor.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF78B450u, 0, 0, false },
	{ "gxos.builtin.calculator", "Calculator", "Calculator", "Calculator", nullptr, "app.calculator", "Utilities", "Built-in guideXOS calculator.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF4690C8u, 0, 0, false },
	{ "gxos.builtin.clock", "Clock", "Clock", nullptr, nullptr, "app.clock", "Utilities", "Built-in guideXOS clock.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.console", "Console", "Console", nullptr, nullptr, "app.console", "System", "Built-in guideXOS console window.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.fileexplorer", "FileExplorer", "FileExplorer", "FileExplorer", "Files", "app.files", "System", "Built-in guideXOS file manager.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFFC8B43Cu, 0, 0, false },
	{ "gxos.builtin.trash", "Trash", "Trash", "Trash", nullptr, "trash.empty", "System", "Built-in guideXOS Trash placeholder.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF9098A4u, 0, 0, false },
	{ "gxos.builtin.taskmanager", "TaskManager", "TaskManager", "TaskManager", nullptr, "app.taskmanager", "System", "Built-in guideXOS task manager.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFFB44646u, 0, 0, false },
	{ "gxos.builtin.paint", "Paint", "Paint", nullptr, nullptr, "app.paint", "Graphics", "Built-in guideXOS paint application.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.imageviewer", "Image Viewer", "ImageViewer", nullptr, nullptr, "app.generic", "Graphics", "Built-in guideXOS image viewer.", BuiltInAvailabilityHosted, 0, 820, 620, true },
	{ "gxos.builtin.onscreenkeyboard", "OnScreenKeyboard", "OnScreenKeyboard", nullptr, nullptr, "app.generic", "Accessibility", "Built-in guideXOS on-screen keyboard.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.shutdowndialog", "ShutdownDialog", "ShutdownDialog", nullptr, nullptr, "app.generic", "System", "Built-in guideXOS shutdown dialog.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.diskmanager", "DiskManager", "DiskManager", "DiskManager", nullptr, "app.diskmanager", "System", "Built-in guideXOS disk manager.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF7050C0u, 0, 0, false },
	{ "gxos.builtin.controlpanel", "ControlPanel", "ControlPanel", nullptr, nullptr, "app.controlpanel", "System", "Built-in guideXOS control panel.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.displayoptions", "DisplayOptions", "DisplayOptions", "DisplayOptions", nullptr, "app.settings", "System", "Built-in guideXOS display options.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF606878u, 0, 0, false },
	{ "guidexos.navigator", "guideXOS Navigator", "guideXOS Navigator", "guideXOS Navigator", nullptr, "app.navigator", "Internet", "Native guideXOS Navigator browser bundled with the OS app model.", BuiltInAvailabilityHosted | BuiltInAvailabilityBareMetal, 0xFF4678BEu, 920, 640, false },
	{ "gxos.builtin.appmodeldemo", "App Model Demo", "App Model Demo", nullptr, nullptr, "app.generic", "Diagnostics", "Built-in guideXOS app-model diagnostics viewer.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.nativeappdebugviewer", "Native App Debug Viewer", "Native App Debug Viewer", nullptr, nullptr, "app.generic", "Diagnostics", "Built-in guideXOS native app diagnostics viewer.", BuiltInAvailabilityHosted, 0, 0, 0, false },
	{ "gxos.builtin.hdinstaller", "HDInstaller", "HDInstaller", nullptr, nullptr, "app.installer", "Installer", "Built-in guideXOS installer entry for supported runtime targets.", BuiltInAvailabilityHosted, 0, 0, 0, false }
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

inline const BuiltInAppMetadata* FindBuiltInAppMetadataByIdentity(const char* identity) {
	const BuiltInAppMetadata* metadata = FindBuiltInAppMetadataByAppId(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByDisplayName(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByLaunchName(identity);
	if (metadata) return metadata;
	metadata = FindBuiltInAppMetadataByKernelAppName(identity);
	if (metadata) return metadata;
	return FindBuiltInAppMetadataByKernelLegacyAlias(identity);
}

} // namespace apps
} // namespace gxos
