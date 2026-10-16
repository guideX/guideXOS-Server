#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include <cstddef>
#include <cstdint>
#endif

namespace gxos {
namespace gui {

// FONT3 deliberately accepts only the TrueType-outline sfnt signature
// (0x00010000). This parser checks bounded structural relationships needed
// for metadata registration; it does not validate executable hinting data or
// prove an outline safe to rasterize.
static const uint32_t kFontTrueTypeMaxFileBytes = 192u * 1024u;
static const uint32_t kFontTrueTypeMaxTables = 64u;
static const uint32_t kFontTrueTypeMaxFaces = 1u;
static const uint32_t kFontTrueTypeMaxGlyphs = 16384u;
static const uint32_t kFontTrueTypeMaxNameRecords = 128u;
static const uint32_t kFontTrueTypeMaxCmapRecords = 32u;
static const uint32_t kFontTrueTypeMaxCmapGroups = 4096u;
static const uint32_t kFontTrueTypeMaxNameBytes = 63u;
static const uint32_t kFontTrueTypeMaxMetadataStringBytes = 256u;
static const uint32_t kFontTrueTypeMaxExternalFonts = 3u;
static const uint32_t kFontTrueTypeMaxPersistentSourceBytes =
	kFontTrueTypeMaxFileBytes * kFontTrueTypeMaxExternalFonts;

enum FontTrueTypeStatus : uint8_t {
	FontTrueTypeOk = 0,
	FontTrueTypeInvalidArgument,
	FontTrueTypeFileTooLarge,
	FontTrueTypeUnsupportedSignature,
	FontTrueTypeUnsupportedCollection,
	FontTrueTypeUnsupportedOutline,
	FontTrueTypeUnsupportedVariant,
	FontTrueTypeInvalidOffsetTable,
	FontTrueTypeTooManyTables,
	FontTrueTypeInvalidTableRange,
	FontTrueTypeDuplicateTable,
	FontTrueTypeMissingRequiredTable,
	FontTrueTypeInvalidRequiredTable,
	FontTrueTypeInvalidNameTable,
	FontTrueTypeUnsupportedUnicodeMap,
};

enum FontTrueTypeCmapFlag : uint8_t {
	FontTrueTypeCmapFormat4 = 1u << 0,
	FontTrueTypeCmapFormat12 = 1u << 1,
	FontTrueTypeCmapUnicodePlatform = 1u << 2,
	FontTrueTypeCmapWindowsBmp = 1u << 3,
	FontTrueTypeCmapWindowsFull = 1u << 4,
};

struct FontTrueTypeMetadata {
	char familyName[kFontTrueTypeMaxNameBytes + 1u];
	char styleName[kFontTrueTypeMaxNameBytes + 1u];
	uint32_t fileBytes;
	uint32_t tableCount;
	uint16_t unitsPerEm;
	uint16_t glyphCount;
	uint16_t numberOfHorizontalMetrics;
	uint16_t advanceWidthMax;
	int16_t ascender;
	int16_t descender;
	int16_t lineGap;
	uint8_t cmapFlags;
	uint8_t faceCount;
	uint8_t reserved[2];
};

// The returned metadata owns no references into inputBytes. Input bytes are
// borrowed only for this call, and all reads are checked against inputLength.
FontTrueTypeStatus FontTrueTypeValidate(const uint8_t* inputBytes,
	size_t inputLength, FontTrueTypeMetadata* outMetadata);

} // namespace gui
} // namespace gxos
