#include "font_glyph_cache_internal.h"
#include "font_truetype_internal.h"

namespace gxos {
namespace gui {
namespace internal {
namespace {

struct FontGlyphCacheEntry {
	FontGlyphCacheKey key;
	uint64_t lastUse;
	uint16_t pixelOffset;
	uint16_t pixelBytes;
	uint8_t valid;
	uint8_t reserved[3];
};

FontGlyphCacheEntry s_entries[kFontGlyphCacheMaxEntries] = {};
uint8_t s_pixels[kFontGlyphCachePixelStorageBytes] = {};
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
		if (clearPixels) {
			for (uint32_t i = 0u; i < entry.pixelBytes; ++i)
				s_pixels[static_cast<uint32_t>(entry.pixelOffset) + i] = 0u;
		}
	}
	entry = {};
}

uint32_t findUnusedEntry()
{
	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i)
		if (!s_entries[i].valid) return i;
	return kFontGlyphCacheMaxEntries;
}

uint32_t selectVictim()
{
	uint32_t victim = kFontGlyphCacheMaxEntries;
	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) {
		if (!s_entries[i].valid) continue;
		if (victim == kFontGlyphCacheMaxEntries ||
			s_entries[i].lastUse < s_entries[victim].lastUse)
			victim = i;
		// Stable lower-index tie breaking makes eviction deterministic.
	}
	return victim;
}

void evictEntry(uint32_t cause)
{
	const uint32_t victim = selectVictim();
	if (victim >= kFontGlyphCacheMaxEntries) return;
	increment(&s_statistics.evictions);
	if (cause == 0u) increment(&s_statistics.entryCapacityEvictions);
	else increment(&s_statistics.coverageCapacityEvictions);
	clearEntry(victim, false);
}

uint64_t nextUseSequence()
{
	if (s_useSequence == static_cast<uint64_t>(-1)) {
		// The cache is tiny. Resetting its age labels preserves bounded,
		// deterministic eviction if the diagnostic lifetime counter saturates.
		for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i)
			s_entries[i].lastUse = 0u;
		s_useSequence = 0u;
	}
	return ++s_useSequence;
}

bool findFreeRange(uint32_t requiredBytes, uint32_t* outOffset)
{
	if (!outOffset || requiredBytes > kFontGlyphCachePixelStorageBytes) return false;
	if (requiredBytes == 0u) {
		*outOffset = 0u;
		return true;
	}
	uint32_t cursor = 0u;
	for (uint32_t scanned = 0u; scanned < kFontGlyphCacheMaxEntries; ++scanned) {
		uint32_t nextEntry = kFontGlyphCacheMaxEntries;
		uint32_t nextOffset = kFontGlyphCachePixelStorageBytes;
		for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) {
			const FontGlyphCacheEntry& entry = s_entries[i];
			if (!entry.valid || entry.pixelBytes == 0u || entry.pixelOffset < cursor)
				continue;
			if (entry.pixelOffset < nextOffset ||
				(entry.pixelOffset == nextOffset && i < nextEntry)) {
				nextEntry = i;
				nextOffset = entry.pixelOffset;
			}
		}
		if (nextEntry == kFontGlyphCacheMaxEntries) {
			if (kFontGlyphCachePixelStorageBytes - cursor < requiredBytes) return false;
			*outOffset = cursor;
			return true;
		}
		if (nextOffset - cursor >= requiredBytes) {
			*outOffset = cursor;
			return true;
		}
		cursor = nextOffset + s_entries[nextEntry].pixelBytes;
		if (cursor > kFontGlyphCachePixelStorageBytes) return false;
	}
	if (kFontGlyphCachePixelStorageBytes - cursor < requiredBytes) return false;
	*outOffset = cursor;
	return true;
}

void compactPixelStorage()
{
	uint32_t order[kFontGlyphCacheMaxEntries] = {};
	uint32_t count = 0u;
	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) {
		if (!s_entries[i].valid || s_entries[i].pixelBytes == 0u) continue;
		uint32_t insertAt = count;
		while (insertAt > 0u &&
			(s_entries[order[insertAt - 1u]].pixelOffset > s_entries[i].pixelOffset ||
			(s_entries[order[insertAt - 1u]].pixelOffset == s_entries[i].pixelOffset &&
			 order[insertAt - 1u] > i))) {
			order[insertAt] = order[insertAt - 1u];
			--insertAt;
		}
		order[insertAt] = i;
		++count;
	}

	uint32_t destination = 0u;
	for (uint32_t position = 0u; position < count; ++position) {
		FontGlyphCacheEntry& entry = s_entries[order[position]];
		const uint32_t source = entry.pixelOffset;
		for (uint32_t byte = 0u; byte < entry.pixelBytes; ++byte)
			s_pixels[destination + byte] = s_pixels[source + byte];
		entry.pixelOffset = static_cast<uint16_t>(destination);
		destination += entry.pixelBytes;
	}
	increment(&s_statistics.coverageCompactions);
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

	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) {
		FontGlyphCacheEntry& entry = s_entries[i];
		if (!entry.valid || !keyEquals(entry.key, key)) continue;
		entry.lastUse = nextUseSequence();
		*outMetrics = lookupMetrics;
		*outCoverage = s_pixels + entry.pixelOffset;
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
	if (rasterStatus != FontStatus::Ok && rasterStatus != FontStatus::FallbackUsed) {
		increment(&s_statistics.rasterizationFailures);
		return rasterStatus;
	}
	if (rasterMetrics.glyphId != lookupMetrics.glyphId ||
		rasterMetrics.xBearingPx != lookupMetrics.xBearingPx ||
		rasterMetrics.yBearingPx != lookupMetrics.yBearingPx ||
		rasterMetrics.widthPx != lookupMetrics.widthPx ||
		rasterMetrics.heightPx != lookupMetrics.heightPx ||
		rasterMetrics.advancePx != lookupMetrics.advancePx ||
		rasterMetrics.sideBearingPx != lookupMetrics.sideBearingPx) {
		increment(&s_statistics.rasterizationFailures);
		return FontStatus::BackendFailure;
	}

	const uint32_t pixelCount = static_cast<uint32_t>(pixelCount64);
	if (pixelCount > kFontGlyphCacheEntryPixelBytes) {
		increment(&s_statistics.cacheBypasses);
		increment(&s_statistics.oversizedBypasses);
		*outMetrics = rasterMetrics;
		*outCoverage = scratch;
		return rasterStatus;
	}

	if (findUnusedEntry() >= kFontGlyphCacheMaxEntries)
		evictEntry(0u);
	uint32_t coverageEvictionAttempts = 0u;
	while (s_statistics.bytesUsed > kFontGlyphCachePixelStorageBytes - pixelCount &&
		coverageEvictionAttempts < kFontGlyphCacheMaxEntries) {
		const uint32_t priorEntryCount = s_statistics.entryCount;
		const uint32_t priorBytesUsed = s_statistics.bytesUsed;
		evictEntry(1u);
		++coverageEvictionAttempts;
		if (s_statistics.entryCount >= priorEntryCount &&
			s_statistics.bytesUsed >= priorBytesUsed) break;
	}
	if (s_statistics.bytesUsed > kFontGlyphCachePixelStorageBytes - pixelCount) {
		increment(&s_statistics.cacheBypasses);
		increment(&s_statistics.admissionBypasses);
		*outMetrics = rasterMetrics;
		*outCoverage = scratch;
		return rasterStatus;
	}

	uint32_t pixelOffset = 0u;
	if (!findFreeRange(pixelCount, &pixelOffset)) {
		compactPixelStorage();
		if (!findFreeRange(pixelCount, &pixelOffset)) {
			// This can only follow corrupted internal accounting. Fail safely and
			// draw from the shared raster scratch without caching the glyph.
			increment(&s_statistics.cacheBypasses);
			increment(&s_statistics.admissionBypasses);
			*outMetrics = rasterMetrics;
			*outCoverage = scratch;
			return rasterStatus;
		}
	}

	const uint32_t victim = findUnusedEntry();
	if (victim >= kFontGlyphCacheMaxEntries) {
		// Entry pressure is handled before coverage allocation above.
		increment(&s_statistics.cacheBypasses);
		increment(&s_statistics.admissionBypasses);
		*outMetrics = rasterMetrics;
		*outCoverage = scratch;
		return rasterStatus;
	}
	FontGlyphCacheEntry& entry = s_entries[victim];
	entry.key = key;
	entry.lastUse = nextUseSequence();
	entry.pixelOffset = static_cast<uint16_t>(pixelOffset);
	entry.pixelBytes = static_cast<uint16_t>(pixelCount);
	entry.valid = 1u;
	entry.reserved[0] = entry.reserved[1] = entry.reserved[2] = 0u;
	for (uint32_t i = 0u; i < pixelCount; ++i)
		s_pixels[pixelOffset + i] = scratch[i];
	++s_statistics.entryCount;
	s_statistics.bytesUsed += pixelCount;
	if (s_statistics.bytesUsed > s_statistics.peakBytesUsed)
		s_statistics.peakBytesUsed = s_statistics.bytesUsed;
	*outMetrics = rasterMetrics;
	*outCoverage = s_pixels + pixelOffset;
	return rasterStatus;
}

void fontGlyphCacheResetLocked()
{
	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) s_entries[i] = {};
	for (uint32_t i = 0u; i < kFontGlyphCachePixelStorageBytes; ++i) s_pixels[i] = 0u;
	s_statistics = {};
	s_useSequence = 0u;
}

void fontGlyphCacheInvalidateFontLocked(uint16_t fontSlot, uint32_t generation)
{
	if (generation == 0u) return;
	for (uint32_t i = 0u; i < kFontGlyphCacheMaxEntries; ++i) {
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
