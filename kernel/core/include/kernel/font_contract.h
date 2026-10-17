#pragma once

#if defined(GXOS_BARE_METAL)
#include "types.h"
#else
#include <cstddef>
#include <cstdint>
#endif

namespace gxos {
namespace gui {

struct FontTrueTypeMetadata;

// FONT2 starts with a deliberately small contract. Font identities are stable
// IDs; handles are process-local slot/generation pairs and must never persist.
typedef uint32_t UnicodeCodePoint;

struct FontId {
	uint64_t value;
};

struct FontHandle {
	uint16_t slot;
	uint16_t reserved;
	uint32_t generation;
};

struct FontSize {
	uint32_t pixelHeight;
};

enum FontCapability : uint32_t {
	FontCapabilityGlyphLookup = 1u << 0,
	FontCapabilityBitmapData = 1u << 1,
	FontCapabilityMeasurement = 1u << 2,
	FontCapabilityUtf8Input = 1u << 3,
	FontCapabilityMonospace = 1u << 4,
	FontCapabilityMetadataOnly = 1u << 5,
	FontCapabilityRasterization = 1u << 6,
	FontCapabilityNoDefaultSelection = 1u << 7,
};

enum class FontStatus : uint8_t {
	Ok = 0,
	FallbackUsed,
	InvalidArgument,
	NotInitialized,
	RegistryFull,
	DuplicateFontId,
	InvalidHandle,
	UnsupportedCodePoint,
	UnsupportedSize,
	InvalidUtf8,
	InputTooLong,
	ArithmeticOverflow,
	BackendFailure,
	GenerationExhausted,
	MetadataOnly,
	MetadataUnavailable,
	CannotUnloadBuiltinFont,
	FontInUse,
	InvalidFontFile,
	UnsupportedFontFormat,
	FontFileTooLarge,
	FontSourceCapacity,
	FontFileNotFound,
	FontFileIoError,
	InvalidGlyphData,
	UnsupportedGlyphFeature,
	BufferTooSmall,
	RasterLimitExceeded,
	FontNotSelectable,
};

enum class FontBitmapFormat : uint8_t {
	None = 0,
	ColumnLsbTop = 1,
};

struct FontMetrics {
	uint32_t pixelSize;
	uint32_t ascentPx;
	uint32_t descentPx;
	uint32_t lineGapPx;
	uint32_t lineHeightPx;
	uint32_t baselinePx;
};

struct GlyphMetrics {
	uint32_t glyphId;
	UnicodeCodePoint codePoint;
	int32_t xBearingPx;
	int32_t yBearingPx;
	uint32_t widthPx;
	uint32_t heightPx;
	uint32_t advancePx;
	int32_t sideBearingPx;
};

struct FontGlyphView {
	GlyphMetrics metrics;
	const uint8_t* bitmap;
	uint32_t bitmapByteLength;
	FontBitmapFormat bitmapFormat;
	uint8_t fallbackUsed;
	uint8_t reserved[2];
	UnicodeCodePoint requestedCodePoint;
};

// pixelWidth and maxLineAdvancePx describe the widest laid-out line.
// lastLineAdvancePx is the advance on the final line. Newlines count as code
// points, but not as glyphs. CRLF is one line break and two code points.
struct TextMeasureResult {
	size_t byteLength;
	uint32_t codePointCount;
	uint32_t glyphCount;
	uint32_t fallbackGlyphCount;
	uint32_t lineCount;
	uint32_t pixelWidth;
	uint32_t pixelHeight;
	uint32_t maxLineAdvancePx;
	uint32_t lastLineAdvancePx;
	size_t errorByteOffset;
};

// Fixed-storage cache diagnostics. All byte counts describe the shared
// grayscale glyph cache, excluding FONT4's shared raster scratch block.
struct FontGlyphCacheStatistics {
	uint64_t hits;
	uint64_t misses;
	uint64_t rasterizations;
	uint64_t evictions;
	uint64_t entryCapacityEvictions;
	uint64_t coverageCapacityEvictions;
	uint64_t coverageCompactions;
	uint64_t cacheBypasses;
	uint64_t oversizedBypasses;
	uint64_t admissionBypasses;
	uint64_t rasterizationFailures;
	uint32_t entryCount;
	uint32_t bytesUsed;
	uint32_t peakBytesUsed;
	uint32_t maxEntries;
	uint32_t maxPixelBytes;
	uint32_t maxGlyphPixels;
	uint32_t maxMetadataBytes;
	uint32_t cacheMemoryBytes;
	uint32_t maxCacheMemoryBytes;
};

struct FontBackend {
	uint32_t capabilities;
	FontStatus (*getMetrics)(const void* context, FontSize size, FontMetrics* outMetrics);
	FontStatus (*lookupGlyph)(const void* context, FontSize size,
		UnicodeCodePoint codePoint, FontGlyphView* outGlyph);
	FontStatus (*rasterizeGlyph)(const void* context, FontSize size,
		UnicodeCodePoint codePoint, uint8_t* coverage, size_t coverageCapacity,
		uint32_t rowStrideBytes, GlyphMetrics* outMetrics);
};

// Backend, context, metadata, and debugName are borrowed for the registration
// lifetime. An optional unregister callback may release owned storage; it is
// invoked under the manager lock and must not re-enter the manager.
struct FontDefinition {
	FontId id;
	const char* debugName;
	const FontBackend* backend;
	const void* context;
	const FontTrueTypeMetadata* trueTypeMetadata;
	void (*onUnregister)(const void* context);
};

static const uint64_t kBuiltinBitmapFontId = 0x47584249544d4150ull; // "GXBITMAP"
static const uint32_t kFontMaxTextBytes = 4096u;
static const uint32_t kFontRegistryCapacity = 4u;
static const uint32_t kBitmapFontPixelHeight = 7u;

FontStatus FontManagerInitialize();
void FontManagerShutdown();
bool FontManagerIsInitialized();
FontStatus FontManagerRegisterFont(const FontDefinition* definition, FontHandle* outHandle);
FontStatus FontManagerUnloadFont(FontHandle handle);
FontStatus FontManagerResolveFont(FontId id, FontHandle* outHandle);
FontStatus FontManagerGetDefaultFont(FontHandle* outHandle);
FontStatus FontManagerSetDefaultFont(FontHandle handle);
uint32_t FontManagerGeneration();
FontStatus FontManagerGetTrueTypeMetadata(FontHandle handle,
	FontTrueTypeMetadata* outMetadata);

// Registration copies a bounded, validated source into manager-owned static
// storage. The memory input is borrowed for this call only. The file variant
// uses the VFS and stores no borrowed path or filename-derived identity.
FontStatus FontTrueTypeLoadMemory(FontId id, const uint8_t* inputBytes,
	size_t inputLength, FontHandle* outHandle,
	FontTrueTypeMetadata* outMetadata);
FontStatus FontTrueTypeLoadFile(const char* path, FontId id,
	FontHandle* outHandle, FontTrueTypeMetadata* outMetadata);

// Finds a NUL terminator within a 4096-byte scan window. The terminator is
// included in the scan bound, so a maximum of 4095 text bytes is accepted.
FontStatus FontUtf8CStringLength(const char* utf8, size_t* outByteLength);
FontStatus FontGetMetrics(FontHandle handle, FontSize size, FontMetrics* outMetrics);
FontStatus FontLookupGlyph(FontHandle handle, FontSize size, UnicodeCodePoint codePoint,
	FontGlyphView* outGlyph);
// Rasterizes one glyph into caller-owned 8-bit grayscale coverage. The backend
// validates rowStrideBytes and coverageCapacity before writing. On BufferTooSmall,
// outMetrics reports the required glyph dimensions and no destination bytes change.
FontStatus FontRasterizeGlyph(FontHandle handle, FontSize size,
	UnicodeCodePoint codePoint, uint8_t* coverage, size_t coverageCapacity,
	uint32_t rowStrideBytes, GlyphMetrics* outMetrics);
FontStatus FontMeasureText(FontHandle handle, FontSize size, const char* utf8,
	size_t byteLength, TextMeasureResult* outResult);
FontStatus FontDrawTextToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	int32_t x, int32_t y, const char* utf8, size_t byteLength, uint32_t color,
	TextMeasureResult* outMeasure);
// Draws antialiased glyphs into an XRGB8888 surface. x is the first glyph
// origin and baselineY is the first line's baseline. Grayscale coverage
// blends foreground RGB over the existing opaque destination pixels.
// destinationBytes includes the final row's visible pixels and must cover the
// complete visible surface described by pitchBytes, bufferWidth, and height.
FontStatus FontDrawGrayscaleTextToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, size_t destinationBytes, uint32_t pitchBytes,
	uint32_t bufferWidth, uint32_t bufferHeight, int32_t x, int32_t baselineY,
	const char* utf8, size_t byteLength, uint32_t color,
	TextMeasureResult* outMeasure);
FontStatus FontDrawCStringToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	int32_t x, int32_t y, const char* utf8, uint32_t color,
	TextMeasureResult* outMeasure);
FontStatus FontGlyphCacheGetStatistics(FontGlyphCacheStatistics* outStatistics);
void FontGlyphCacheReset();

const FontDefinition* FontBuiltinBitmapDefinition();

} // namespace gui
} // namespace gxos
