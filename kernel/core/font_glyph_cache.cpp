#include "font_glyph_cache_internal.h"
#include "font_truetype_internal.h"

namespace gxos {
namespace gui {
namespace internal {
namespace {

struct FontGlyphCacheEntry {
	FontGlyphCacheKey key;
	GlyphMetrics metrics;
	uint64_t lastUse;
	uint32_t pixelBytes;
	uint8_t valid;
	uint8_t reserved[3];
};

FontGlyphCacheEntry s_entries[kFontGlyphCacheMaxEntries] = {};
uint8_t s_pixels[kFontGlyphCacheMaxEntries][kFontGlyphCacheEntryPixelBytes] = {};
FontGlyphCacheStatistics s_statistics = {};
uint64_t s_useSequence = 0;

static_assert(sizeof(s_entries) <= kFontGlyphCacheMaxMetadataBytes,
	"glyph cache metadata exceeds FONT5 budget");
static_assert(sizeof(s_entries) + sizeof(s_pixels) + sizeof(s_statistics) +
	sizeof(s_useSequence) <= kFontGlyphCacheMaxMemoryBytes,
	"glyph cache storage exceeds FONT5 total budget");

void increment(uint64_t* value)
{
	if (value && *value != static_cast<uint64_t>(-1)) ++*value;
}

bool keyEquals(const FontGlyphCacheKey& left, const FontGlyphCacheKey& right)
{
	return left.fontId.value == right.fontId.value &&
		left.fontSlot == right.fontSlot &&
		left.fontGeneration == right.fontGeneration &&
		left.glyphId == right.glyphId &&
		left.pixelSize == right.pixelSize &&
		left.rasterConfig == right.rasterConfig;
}

void clearEntry(uint32_t index, bool clearPixels)
{
	if (index >= kFontGlyphCacheMaxEntries) return;
	FontGlyphCacheEntry& entry = s_entries[index];
	if (entry.valid) {
		if (s_statistics.entryCount != 0u) --s_statistics.entryCount;
		if (entry.pixelBytes <= s_statistics.bytesUsed)
			s_statistics.bytesUsed -= entry.pixelBytes;
		else
			s_statistics.bytesUsed = 0u;
	}
	entry = {};
	if (clearPixels) {
		for (uint32_t i = 0; i < kFontGlyphCacheEntryPixelBytes; ++i)
			s_pixels[index][i] = 0u;
	}
}

uint32_t selectVictim()
{
	for (uint32_t i = 0; i < kFontGlyphCacheMaxEntries; ++i)
		if (!s_entries[i].valid) return i;
	uint32_t victim = 0u;
	for (uint32_t i = 1u; i < kFontGlyphCacheMaxEntries; ++i) {
		// Stable lower-index tie breaking makes eviction deterministic.
		if (s_entries[i].lastUse < s_entries[victim].lastUse) victim = i;
	}
	return victim;
}

uint64_t nextUseSequence()
{
	if (s_useSequence == static_cast<uint64_t>(-1)) {
		// The cache is tiny. Resetting its age labels preserves bounded,
		// deterministic eviction if the diagnostic lifetime counter saturates.
		for (uint32_t i = 0; i < kFontGlyphCacheMaxEntries; ++i)
			s_entries[i].lastUse = 0u;
		s_useSequence = 0u;
	}
	return ++s_useSequence;
}

bool sameRasterMetrics(const GlyphMetrics& left, const GlyphMetrics& right)
{
	return left.glyphId == right.glyphId &&
		left.xBearingPx == right.xBearingPx &&
		left.yBearingPx == right.yBearingPx &&
		left.widthPx == right.widthPx &&
		left.heightPx == right.heightPx &&
		left.advancePx == right.advancePx &&
		left.sideBearingPx == right.sideBearingPx;
}

} // namespace

FontStatus fontGlyphCacheGetOrRasterize(const FontGlyphCacheKey& key,
	const GlyphMetrics& lookupMetrics, FontGlyphCacheRasterizer rasterizer,
	void* rasterizerContext, GlyphMetrics* outMetrics, const uint8_t** outCoverage,
	bool* outCacheHit)
{
	if (outMetrics) *outMetrics = {};
	if (outCoverage) *outCoverage = nullptr;
	if (outCacheHit) *outCacheHit = false;
	if (!outMetrics || !outCoverage || !outCacheHit || !rasterizer ||
		key.fontId.value == 0u || key.fontGeneration == 0u ||
		key.pixelSize == 0u || key.rasterConfig == 0u)
		return FontStatus::InvalidArgument;

	for (uint32_t i = 0; i < kFontGlyphCacheMaxEntries; ++i) {
		FontGlyphCacheEntry& entry = s_entries[i];
		if (!entry.valid || !keyEquals(entry.key, key)) continue;
		entry.lastUse = nextUseSequence();
		*outMetrics = entry.metrics;
		*outCoverage = s_pixels[i];
		*outCacheHit = true;
		increment(&s_statistics.hits);
		return FontStatus::Ok;
	}

	increment(&s_statistics.misses);
	if (lookupMetrics.widthPx > 128u || lookupMetrics.heightPx > 128u)
		return FontStatus::RasterLimitExceeded;
	const uint64_t pixelCount64 = static_cast<uint64_t>(lookupMetrics.widthPx) *
		lookupMetrics.heightPx;
	if (pixelCount64 > 16384u) return FontStatus::RasterLimitExceeded;
	uint8_t* scratch = fontTrueTypeRasterScratchCoverage();
	const size_t scratchCapacity = fontTrueTypeRasterScratchCoverageCapacity();
	if (!scratch || scratchCapacity < static_cast<size_t>(pixelCount64))
		return FontStatus::BackendFailure;

	GlyphMetrics rasterMetrics = {};
	increment(&s_statistics.rasterizations);
	const uint32_t stride = lookupMetrics.widthPx;
	const FontStatus rasterStatus = rasterizer(rasterizerContext, scratch,
		scratchCapacity, stride, &rasterMetrics);
	if (rasterStatus != FontStatus::Ok && rasterStatus != FontStatus::FallbackUsed)
		return rasterStatus;
	if (!sameRasterMetrics(lookupMetrics, rasterMetrics)) return FontStatus::BackendFailure;

	const uint32_t pixelCount = static_cast<uint32_t>(pixelCount64);
	if (pixelCount > kFontGlyphCacheEntryPixelBytes) {
		*outMetrics = rasterMetrics;
		*outCoverage = scratch;
		return rasterStatus;
	}

	const uint32_t victim = selectVictim();
	if (s_entries[victim].valid) {
		increment(&s_statistics.evictions);
		clearEntry(victim, false);
	}
	FontGlyphCacheEntry& entry = s_entries[victim];
	entry.key = key;
	entry.metrics = rasterMetrics;
	entry.lastUse = nextUseSequence();
	entry.pixelBytes = pixelCount;
	entry.valid = 1u;
	entry.reserved[0] = entry.reserved[1] = entry.reserved[2] = 0u;
	for (uint32_t row = 0; row < rasterMetrics.heightPx; ++row) {
		for (uint32_t column = 0; column < rasterMetrics.widthPx; ++column) {
			const size_t sourceIndex = static_cast<size_t>(row) * stride + column;
			const size_t destinationIndex = static_cast<size_t>(row) * rasterMetrics.widthPx + column;
			s_pixels[victim][destinationIndex] = scratch[sourceIndex];
		}
	}
	++s_statistics.entryCount;
	s_statistics.bytesUsed += pixelCount;
	if (s_statistics.bytesUsed > s_statistics.peakBytesUsed)
		s_statistics.peakBytesUsed = s_statistics.bytesUsed;
	*outMetrics = rasterMetrics;
	*outCoverage = s_pixels[victim];
	return rasterStatus;
}

void fontGlyphCacheResetLocked()
{
	for (uint32_t i = 0; i < kFontGlyphCacheMaxEntries; ++i) clearEntry(i, true);
	s_statistics = {};
	s_useSequence = 0u;
}

void fontGlyphCacheInvalidateFontLocked(uint16_t fontSlot, uint32_t generation)
{
	if (generation == 0u) return;
	for (uint32_t i = 0; i < kFontGlyphCacheMaxEntries; ++i) {
		const FontGlyphCacheEntry& entry = s_entries[i];
		if (entry.valid && entry.key.fontSlot == fontSlot &&
			entry.key.fontGeneration == generation) clearEntry(i, true);
	}
}

void fontGlyphCacheGetStatisticsLocked(FontGlyphCacheStatistics* outStatistics)
{
	if (!outStatistics) return;
	*outStatistics = s_statistics;
	outStatistics->maxEntries = kFontGlyphCacheMaxEntries;
	outStatistics->maxPixelBytes = kFontGlyphCachePixelStorageBytes;
	outStatistics->maxGlyphPixels = kFontGlyphCacheEntryPixelBytes;
	outStatistics->maxMetadataBytes = kFontGlyphCacheMaxMetadataBytes;
	outStatistics->cacheMemoryBytes = static_cast<uint32_t>(sizeof(s_entries) +
		sizeof(s_pixels) + sizeof(s_statistics) + sizeof(s_useSequence));
	outStatistics->maxCacheMemoryBytes = kFontGlyphCacheMaxMemoryBytes;
}

} // namespace internal
} // namespace gui
} // namespace gxos
