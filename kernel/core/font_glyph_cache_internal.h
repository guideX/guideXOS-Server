#pragma once

#include "include/kernel/font_contract.h"

namespace gxos {
namespace gui {
namespace internal {

// FONT11 keeps metadata and a variable-size coverage arena in fixed arrays, so
// it needs no heap allocation. Cache helpers are called only while the FONT2
// manager lock is held. The lock order is manager -> cache; the cache never
// calls the manager.
static const uint32_t kFontGlyphCacheMaxEntries = 72u;
static const uint32_t kFontGlyphCacheEntryPixelBytes = 4096u;
static const uint32_t kFontGlyphCachePixelStorageBytes = 65536u;
static const uint32_t kFontGlyphCacheMaxMetadataBytes = 4096u;
static const uint32_t kFontGlyphCacheMaxMemoryBytes = 72u * 1024u;
static const uint32_t kFontGlyphRasterConfigGray8UnhintedV1 = 0x00010001u;

struct FontGlyphCacheKey {
	FontId fontId;
	uint16_t fontSlot;
	uint16_t reserved;
	uint32_t fontGeneration;
	uint32_t glyphId;
	uint32_t pixelSize;
	uint32_t rasterConfig;
};

typedef FontStatus (*FontGlyphCacheRasterizer)(void* context, uint8_t* pixels,
	size_t capacity, uint32_t rowStrideBytes, GlyphMetrics* outMetrics);

// Called with the FONT2 manager lock held. A successful uncached result points
// into FONT4's shared raster scratch and is valid until the next raster call.
FontStatus fontGlyphCacheGetOrRasterize(const FontGlyphCacheKey& key,
	const GlyphMetrics& lookupMetrics, FontGlyphCacheRasterizer rasterizer,
	void* rasterizerContext, GlyphMetrics* outMetrics, const uint8_t** outCoverage,
	bool* outCacheHit);
void fontGlyphCacheResetLocked();
void fontGlyphCacheInvalidateFontLocked(uint16_t fontSlot, uint32_t generation);
void fontGlyphCacheGetStatisticsLocked(FontGlyphCacheStatistics* outStatistics);

// FONT4 owns this coverage area inside its single shared RasterScratch block.
// It is safe to share because manager operations serialize FONT4 rasterization
// and FONT5 never returns from a cache miss while still using the scratch.
uint8_t* fontTrueTypeRasterScratchCoverage();
size_t fontTrueTypeRasterScratchCoverageCapacity();

} // namespace internal
} // namespace gui
} // namespace gxos
