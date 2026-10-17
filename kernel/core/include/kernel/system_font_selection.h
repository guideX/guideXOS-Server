#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#include "font_contract.h"
#include "font_truetype.h"
#else
#include <cstdint>
#include "font_contract.h"
#include "font_truetype.h"
#endif

namespace gxos {
namespace gui {

static const uint32_t kSystemFontMaxAvailableFonts = kFontTrueTypeMaxExternalFonts;
static const uint32_t kSystemFontMaxDiscoveredEntries = 64u;
static const uint32_t kSystemFontMinSelectablePixelSize = 8u;
static const uint32_t kSystemFontMaxSelectablePixelSize = 48u;
static const uint32_t kSystemFontPreferenceFormatVersion = 1u;

enum class SystemFontSelectionStatus : uint8_t {
	Ok = 0,
	NotInitialized,
	InvalidArgument,
	DirectoryUnavailable,
	DiscoveryLimitReached,
	RegistryCapacity,
	SourceCapacity,
	InvalidFont,
	UnsupportedFont,
	NotFound,
	StaleIdentity,
	SizeOutOfRange,
	PreparationFailed,
	CandidateStale,
	PreferenceCorrupt,
	PreferenceUnsupported,
	PersistenceFailed,
	GenerationExhausted,
	NoPreviousSelection,
	RollbackFailed,
	OutputTooSmall,
};

enum class SystemFontIdentityKind : uint8_t {
	BuiltinBitmap = 0,
	TrueType = 1,
};

// Stable identity is a validated canonical VFS locator plus metadata and a
// content fingerprint. It is independent of FontHandle slot/generation.
struct SystemFontIdentity {
	char canonicalPath[kFontTrueTypeMaxNameBytes * 4u + 4u];
	char familyName[kFontTrueTypeMaxNameBytes + 1u];
	char styleName[kFontTrueTypeMaxNameBytes + 1u];
	uint32_t fileBytes;
	uint64_t contentFingerprint;
	uint64_t stableId;
	SystemFontIdentityKind kind;
	uint8_t reserved[7];
};

struct SystemFontFaceInfo {
	SystemFontIdentity identity;
	FontTrueTypeMetadata metadata;
	uint8_t available;
	uint8_t active;
	uint8_t reserved[2];
};

struct SystemFontSelectionSnapshot {
	SystemFontIdentity identity;
	uint32_t pixelSize;
	uint32_t selectionGeneration;
	uint8_t isPersisted;
	uint8_t reserved[3];
};

struct SystemFontCandidate {
	uint64_t identity;
	uint32_t catalogGeneration;
	uint32_t token;
	uint32_t pixelSize;
};

struct SystemFontConfiguredPreference {
	SystemFontIdentity identity;
	uint32_t pixelSize;
	uint64_t sequence;
	uint8_t isPersisted;
	uint8_t reserved[7];
};

// Initializes the existing fixed-capacity font manager, scans only direct
// children of /system/fonts, and restores one validated preference. Missing
// storage or fonts leave the built-in bitmap face active and do not fail boot.
SystemFontSelectionStatus SystemFontSelectionInitialize();
void SystemFontSelectionShutdown();
SystemFontSelectionStatus SystemFontSelectionRefreshCatalog();
SystemFontSelectionStatus SystemFontSelectionGetActive(
	SystemFontSelectionSnapshot* outSnapshot);
SystemFontSelectionStatus SystemFontSelectionGetConfigured(
	SystemFontConfiguredPreference* outPreference);
SystemFontSelectionStatus SystemFontSelectionGetAvailable(
	SystemFontFaceInfo* output, uint32_t capacity, uint32_t* outCount);
SystemFontSelectionStatus SystemFontSelectionPrepare(
	const SystemFontIdentity* identity, uint32_t pixelSize,
	SystemFontCandidate* outCandidate);
SystemFontSelectionStatus SystemFontSelectionPrepareActiveSize(
	uint32_t pixelSize, SystemFontCandidate* outCandidate);
SystemFontSelectionStatus SystemFontSelectionApply(
	const SystemFontCandidate* candidate);
SystemFontSelectionStatus SystemFontSelectionRevert();
SystemFontSelectionStatus SystemFontSelectionResetToBuiltin();
SystemFontSelectionStatus SystemFontSelectionPreferenceStatus();
const char* SystemFontSelectionStatusName(SystemFontSelectionStatus status);

} // namespace gui
} // namespace gxos
