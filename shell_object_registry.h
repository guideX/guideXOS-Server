#pragma once

#include "built_in_app_metadata.h"

#include <stddef.h>

namespace gxos {
namespace apps {

enum class ShellObjectKind {
	Desktop = 0,
	Root,
	FileManager,
	Folder,
	SystemPanel,
	Trash
};

enum class ShellObjectTargetKind {
	VirtualObject = 0,
	FilesystemFolder,
	AppSystemPanel
};

struct ShellObjectRegistryRecord {
	const char* shellObjectId;
	const char* displayName;
	const char* canonicalLaunchTargetName;
	const char* const* aliases;
	size_t aliasCount;
	ShellObjectKind kind;
	const char* defaultHandlerAppIdentity;
	bool activeTypedDispatchMayOwn;
	bool systemOnly;
	bool riskyDestructive;
	bool shouldWriteRecentPrograms;
	ShellObjectTargetKind targetKind;
	const char* canonicalTargetValue;
};

namespace detail {
static const char* const kDesktopAliases[] = {
	"Desktop",
	"Desktop Home",
	"Go to Desktop"
};

static const char* const kThisSystemAliases[] = {
	"This System",
	"Computer"
};

static const char* const kFilesAliases[] = {
	"Files",
	"File Manager",
	"File Explorer",
	"FileExplorer"
};

static const char* const kDocumentsAliases[] = {
	"Documents"
};

static const char* const kPicturesAliases[] = {
	"Pictures"
};

static const char* const kMusicAliases[] = {
	"Music"
};

static const char* const kNetworkAliases[] = {
	"Network"
};

static const char* const kSettingsAliases[] = {
	"Settings",
	"System Settings",
	"Display Options",
	"Display Settings",
	"Desktop Background",
	"Wallpaper"
};

static const char* const kShellControlPanelAliases[] = {
	"Control Panel"
};

static const char* const kTrashAliases[] = {
	"Trash"
};

inline bool shellObjectTextEquals(const char* a, const char* b) {
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

static const ShellObjectRegistryRecord kShellObjectRegistry[] = {
	{ "gxos.shell.desktop", "Desktop", "DesktopHome", detail::kDesktopAliases, sizeof(detail::kDesktopAliases) / sizeof(detail::kDesktopAliases[0]), ShellObjectKind::Desktop, nullptr, false, true, false, false, ShellObjectTargetKind::VirtualObject, "DesktopHome" },
	{ "gxos.shell.this-system", "This System", "FileExplorer", detail::kThisSystemAliases, sizeof(detail::kThisSystemAliases) / sizeof(detail::kThisSystemAliases[0]), ShellObjectKind::Root, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::FilesystemFolder, "/" },
	{ "gxos.shell.files", "Files", "FileExplorer", detail::kFilesAliases, sizeof(detail::kFilesAliases) / sizeof(detail::kFilesAliases[0]), ShellObjectKind::FileManager, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::AppSystemPanel, "" },
	{ "gxos.shell.documents", "Documents", "FileExplorer", detail::kDocumentsAliases, sizeof(detail::kDocumentsAliases) / sizeof(detail::kDocumentsAliases[0]), ShellObjectKind::Folder, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::FilesystemFolder, "/Documents" },
	{ "gxos.shell.pictures", "Pictures", "FileExplorer", detail::kPicturesAliases, sizeof(detail::kPicturesAliases) / sizeof(detail::kPicturesAliases[0]), ShellObjectKind::Folder, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::FilesystemFolder, "/Pictures" },
	{ "gxos.shell.music", "Music", "FileExplorer", detail::kMusicAliases, sizeof(detail::kMusicAliases) / sizeof(detail::kMusicAliases[0]), ShellObjectKind::Folder, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::FilesystemFolder, "/Music" },
	{ "gxos.shell.network", "Network", "FileExplorer", detail::kNetworkAliases, sizeof(detail::kNetworkAliases) / sizeof(detail::kNetworkAliases[0]), ShellObjectKind::Folder, "gxos.builtin.fileexplorer", true, true, false, true, ShellObjectTargetKind::FilesystemFolder, "/Network" },
	{ "gxos.shell.settings", "Settings", "DisplayOptions", detail::kSettingsAliases, sizeof(detail::kSettingsAliases) / sizeof(detail::kSettingsAliases[0]), ShellObjectKind::SystemPanel, "gxos.builtin.displayoptions", true, true, false, true, ShellObjectTargetKind::AppSystemPanel, "" },
	{ "gxos.shell.control-panel", "Control Panel", "ControlPanel", detail::kShellControlPanelAliases, sizeof(detail::kShellControlPanelAliases) / sizeof(detail::kShellControlPanelAliases[0]), ShellObjectKind::SystemPanel, "gxos.builtin.controlpanel", true, true, false, true, ShellObjectTargetKind::AppSystemPanel, "" },
	{ "gxos.shell.trash-open", "Trash", "Trash", detail::kTrashAliases, sizeof(detail::kTrashAliases) / sizeof(detail::kTrashAliases[0]), ShellObjectKind::Trash, "gxos.builtin.trash", true, true, false, true, ShellObjectTargetKind::VirtualObject, "" }
};

static const size_t kShellObjectRegistryCount = sizeof(kShellObjectRegistry) / sizeof(kShellObjectRegistry[0]);

inline const char* ShellObjectKindToString(ShellObjectKind kind) {
	switch (kind) {
	case ShellObjectKind::Desktop: return "desktop";
	case ShellObjectKind::Root: return "root";
	case ShellObjectKind::FileManager: return "file-manager";
	case ShellObjectKind::Folder: return "folder";
	case ShellObjectKind::SystemPanel: return "system-panel";
	case ShellObjectKind::Trash: return "trash";
	default: return "unknown";
	}
}

inline const char* ShellObjectTargetKindToString(ShellObjectTargetKind kind) {
	switch (kind) {
	case ShellObjectTargetKind::VirtualObject: return "virtual-object";
	case ShellObjectTargetKind::FilesystemFolder: return "filesystem-folder";
	case ShellObjectTargetKind::AppSystemPanel: return "app/system-panel";
	default: return "unknown";
	}
}

inline const ShellObjectRegistryRecord* FindShellObjectRegistryRecordById(const char* shellObjectId) {
	if (!shellObjectId || !shellObjectId[0]) return nullptr;
	for (size_t i = 0; i < kShellObjectRegistryCount; ++i) {
		if (detail::shellObjectTextEquals(kShellObjectRegistry[i].shellObjectId, shellObjectId)) return &kShellObjectRegistry[i];
	}
	return nullptr;
}

inline const ShellObjectRegistryRecord* FindShellObjectRegistryRecordByAlias(const char* alias) {
	if (!alias || !alias[0]) return nullptr;
	for (size_t i = 0; i < kShellObjectRegistryCount; ++i) {
		const ShellObjectRegistryRecord& record = kShellObjectRegistry[i];
		if (detail::shellObjectTextEquals(record.shellObjectId, alias) ||
			detail::shellObjectTextEquals(record.displayName, alias) ||
			detail::shellObjectTextEquals(record.canonicalLaunchTargetName, alias) ||
			(record.defaultHandlerAppIdentity && detail::shellObjectTextEquals(record.defaultHandlerAppIdentity, alias))) {
			return &record;
		}
		for (size_t j = 0; j < record.aliasCount; ++j) {
			if (detail::shellObjectTextEquals(record.aliases[j], alias)) return &record;
		}
	}
	return nullptr;
}

inline bool ShellObjectRegistryHandlerResolvesToBuiltInRegistry(const ShellObjectRegistryRecord& record) {
	if (!record.defaultHandlerAppIdentity || !record.defaultHandlerAppIdentity[0]) return true;
	return FindBuiltInAppMetadataByAppId(record.defaultHandlerAppIdentity) != nullptr;
}

inline bool ShellObjectRegistryAliasResolves(const char* alias, const ShellObjectRegistryRecord** outRecord = nullptr) {
	const ShellObjectRegistryRecord* record = FindShellObjectRegistryRecordByAlias(alias);
	if (outRecord) *outRecord = record;
	return record != nullptr;
}

} // namespace apps
} // namespace gxos
