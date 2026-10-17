#include "kernel/core/include/kernel/font_contract.h"
#include "kernel/core/include/kernel/font_text_layout.h"
#include "kernel/core/include/kernel/vfs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

using namespace gxos::gui;

struct Surface {
	const char* surface;
	const char* label;
	uint32_t pixelSize;
	uint32_t boxWidth;
	uint32_t boxHeight;
};

static const Surface kWorkload[] = {
	{"window-title", "guideXOS Navigator", 13u, 680u, 20u},
	{"window-title", "Terminal - guideXOS Shell", 13u, 680u, 20u},
	{"taskbar-label", "guideXOS Navigator", 12u, 142u, 24u},
	{"taskbar-label", "Terminal", 12u, 142u, 24u},
	{"start-menu-left", "guideXOS Navigator", 12u, 202u, 24u},
	{"start-menu-left", "Console", 12u, 202u, 24u},
	{"start-menu-right", "Computer", 12u, 126u, 24u},
	{"start-menu-right", "Documents", 12u, 126u, 24u},
	{"start-menu-right", "Pictures", 12u, 126u, 24u},
	{"start-menu-right", "Music", 12u, 126u, 24u},
	{"start-menu-right", "Network", 12u, 126u, 24u},
	{"start-menu-right", "Control Panel", 12u, 126u, 24u},
	{"start-menu-footer", "All Programs", 12u, 128u, 24u},
	{"start-menu-footer", "Sleep", 12u, 48u, 24u},
	{"start-menu-footer", "Restart", 12u, 64u, 24u},
	{"start-menu-footer", "Shut Down", 12u, 80u, 24u},
	{"start-menu-header", "User", 17u, 320u, 30u},
};

static uint32_t s_pixels[1280u * 768u];
static uint64_t s_workPerFrame = 0u;
static uint32_t s_checks = 0u;
static uint32_t s_failures = 0u;

struct SeenKey {
	uint32_t glyphId;
	uint32_t pixelSize;
	uint32_t count;
};

static SeenKey s_seenKeys[256] = {};
static uint32_t s_seenKeyCount = 0u;
static uint32_t s_repeatedKeyOccurrences = 0u;

bool successful(FontStatus status)
{
	return status == FontStatus::Ok || status == FontStatus::FallbackUsed;
}

uint32_t countCodePoints(const char* text, size_t length)
{
	uint32_t count = 0u;
	for (size_t i = 0u; i < length; ++i)
		if ((static_cast<uint8_t>(text[i]) & 0xc0u) != 0x80u) ++count;
	return count;
}

bool drawOne(FontHandle font, const Surface& surface, uint32_t row)
{
	char fitted[256] = {};
	FontTextLineLayout layout = {};
	const size_t sourceLength = std::strlen(surface.label);
	const FontStatus layoutStatus = FontTextLayoutSingleLine(font,
		{surface.pixelSize}, surface.label, sourceLength, surface.boxWidth,
		FontTextOverflowPolicy::Ellipsis, fitted, sizeof(fitted), &layout);
	if (!successful(layoutStatus) || layout.lineHeightPx > surface.boxHeight) return false;
	if (std::strncmp(surface.surface, "taskbar-clock", 13u) == 0 &&
		(std::strcmp(fitted, surface.label) != 0 || layout.truncated)) return false;
	for (size_t offset = 0u; offset < layout.outputByteLength; ++offset) {
		const uint8_t first = static_cast<uint8_t>(fitted[offset]);
		if (first >= 0x80u && (first & 0xc0u) == 0x80u) continue;
		FontGlyphView glyph = {};
		if (!successful(FontLookupGlyph(font, {surface.pixelSize}, first, &glyph)))
			return false;
		uint32_t keyIndex = 0u;
		while (keyIndex < s_seenKeyCount &&
			(s_seenKeys[keyIndex].glyphId != glyph.metrics.glyphId ||
			 s_seenKeys[keyIndex].pixelSize != surface.pixelSize)) ++keyIndex;
		if (keyIndex == s_seenKeyCount) {
			if (s_seenKeyCount >= sizeof(s_seenKeys) / sizeof(s_seenKeys[0])) return false;
			s_seenKeys[s_seenKeyCount++] = {glyph.metrics.glyphId,
				surface.pixelSize, 0u};
		} else {
			++s_repeatedKeyOccurrences;
		}
		if (s_seenKeys[keyIndex].count != 0xffffffffu) ++s_seenKeys[keyIndex].count;
	}
	const int32_t originX = 8;
	const int32_t baselineY = static_cast<int32_t>(row * 24u + 16u);
	const FontStatus drawStatus = FontDrawGrayscaleTextToBuffer(font,
		{surface.pixelSize}, s_pixels, sizeof(s_pixels), 1280u * 4u,
		1280u, 768u, originX, baselineY, fitted, layout.outputByteLength,
		0xffe0e0e0u, nullptr);
	if (!successful(drawStatus)) return false;
	s_workPerFrame += countCodePoints(fitted, layout.outputByteLength);
	return true;
}

void printDelta(const char* phase, const Surface& surface,
	const FontGlyphCacheStatistics& before, const FontGlyphCacheStatistics& after)
{
	std::printf("%s surface=%s label=\"%s\" px=%u work_codepoints=%u hits=%llu misses=%llu rasters=%llu evictions=%llu entry_evictions=%llu coverage_evictions=%llu bypasses=%llu entries=%u bytes=%u peak=%u\n",
		phase, surface.surface, surface.label, surface.pixelSize,
		countCodePoints(surface.label, std::strlen(surface.label)),
		static_cast<unsigned long long>(after.hits - before.hits),
		static_cast<unsigned long long>(after.misses - before.misses),
		static_cast<unsigned long long>(after.rasterizations - before.rasterizations),
		static_cast<unsigned long long>(after.evictions - before.evictions),
		static_cast<unsigned long long>(after.entryCapacityEvictions - before.entryCapacityEvictions),
		static_cast<unsigned long long>(after.coverageCapacityEvictions - before.coverageCapacityEvictions),
		static_cast<unsigned long long>(after.cacheBypasses - before.cacheBypasses),
		after.entryCount, after.bytesUsed, after.peakBytesUsed);
}

bool drawFrame(FontHandle font, const char* phase)
{
	s_workPerFrame = 0u;
	s_seenKeyCount = 0u;
	s_repeatedKeyOccurrences = 0u;
	for (uint32_t i = 0u; i < sizeof(s_seenKeys) / sizeof(s_seenKeys[0]); ++i)
		s_seenKeys[i] = {};
	FontGlyphCacheStatistics frameBefore = {};
	FontGlyphCacheGetStatistics(&frameBefore);
	for (uint32_t i = 0u; i < sizeof(kWorkload) / sizeof(kWorkload[0]); ++i) {
		FontGlyphCacheStatistics before = {};
		FontGlyphCacheStatistics after = {};
		++s_checks;
		if (FontGlyphCacheGetStatistics(&before) != FontStatus::Ok ||
			!drawOne(font, kWorkload[i], i) ||
			FontGlyphCacheGetStatistics(&after) != FontStatus::Ok) {
			++s_failures;
			return false;
		}
		printDelta(phase, kWorkload[i], before, after);
	}
	FontGlyphCacheStatistics stats = {};
	if (FontGlyphCacheGetStatistics(&stats) != FontStatus::Ok) return false;
	const bool warmReuse = std::strcmp(phase, "warm") != 0 ||
		(stats.misses == frameBefore.misses &&
		 stats.rasterizations == frameBefore.rasterizations);
	++s_checks;
	if (!warmReuse) ++s_failures;
	std::printf("%s frame draws=%u fitted_codepoints=%llu unique_keys=%u repeated_key_occurrences=%u hits=%llu misses=%llu rasters=%llu evictions=%llu entries=%u bytes=%u peak=%u entry_evictions=%llu coverage_evictions=%llu compactions=%llu bypasses=%llu oversized_bypasses=%llu admission_bypasses=%llu raster_failures=%llu max_entries=%u coverage_capacity=%u cache_bytes=%u warm_reuse=%s\n",
		phase, static_cast<unsigned>(sizeof(kWorkload) / sizeof(kWorkload[0])),
		static_cast<unsigned long long>(s_workPerFrame), s_seenKeyCount,
		s_repeatedKeyOccurrences,
		static_cast<unsigned long long>(stats.hits),
		static_cast<unsigned long long>(stats.misses),
		static_cast<unsigned long long>(stats.rasterizations),
		static_cast<unsigned long long>(stats.evictions), stats.entryCount,
		stats.bytesUsed, stats.peakBytesUsed,
		static_cast<unsigned long long>(stats.entryCapacityEvictions),
		static_cast<unsigned long long>(stats.coverageCapacityEvictions),
		static_cast<unsigned long long>(stats.coverageCompactions),
		static_cast<unsigned long long>(stats.cacheBypasses),
		static_cast<unsigned long long>(stats.oversizedBypasses),
		static_cast<unsigned long long>(stats.admissionBypasses),
		static_cast<unsigned long long>(stats.rasterizationFailures), stats.maxEntries,
		stats.maxPixelBytes, stats.cacheMemoryBytes, warmReuse ? "yes" : "no");
	for (uint32_t i = 0u; i < s_seenKeyCount; ++i) {
		if (s_seenKeys[i].count > 1u)
			std::printf("%s repeated-glyph-key=glyph:%u px:%u occurrences:%u\n",
				phase, s_seenKeys[i].glyphId, s_seenKeys[i].pixelSize,
				s_seenKeys[i].count);
	}
	return warmReuse;
}

bool drawClockLine(FontHandle font, const char* phase, const char* surface,
	const char* label, uint32_t row)
{
	const Surface descriptor = {surface, label, 12u, 66u, 16u};
	FontGlyphCacheStatistics before = {};
	FontGlyphCacheStatistics after = {};
	++s_checks;
	if (FontGlyphCacheGetStatistics(&before) != FontStatus::Ok ||
		!drawOne(font, descriptor, row) ||
		FontGlyphCacheGetStatistics(&after) != FontStatus::Ok) {
		++s_failures;
		return false;
	}
	const bool unchangedUpdate = std::strcmp(phase, "clock-cold") == 0 ||
		(after.misses == before.misses &&
		 after.rasterizations == before.rasterizations);
	++s_checks;
	if (!unchangedUpdate) ++s_failures;
	printDelta(phase, descriptor, before, after);
	return unchangedUpdate;
}

} // namespace

int main()
{
	std::ifstream file("assets/Fonts/roboto/Roboto-Italic.ttf", std::ios::binary);
	const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>()};
	if (bytes.empty()) {
		std::fprintf(stderr, "FONT11 workload could not read Roboto-Italic.ttf\n");
		return 2;
	}
	if (FontManagerInitialize() != FontStatus::Ok) return 3;
	FontHandle font = {};
	if (FontTrueTypeLoadMemory({0x464f4e5431310001ull}, bytes.data(),
		bytes.size(), &font, nullptr) != FontStatus::Ok) return 4;
	FontGlyphCacheReset();
	std::printf("workload=FONT10 desktop screenshot 01; selected=Roboto-Italic 17px; roles=title:13px taskbar/menu:12px profile:17px; text_surfaces=%u\n",
		static_cast<unsigned>(sizeof(kWorkload) / sizeof(kWorkload[0])));
	const bool coldOk = drawFrame(font, "cold");
	const bool clockColdOk = drawClockLine(font, "clock-cold", "taskbar-clock-time",
		"19:08", 17u) && drawClockLine(font, "clock-cold", "taskbar-clock-date",
		"10/17/2026", 18u);
	const bool warmOk = drawFrame(font, "warm");
	const bool clockWarmOk = drawClockLine(font, "clock-warm", "taskbar-clock-time",
		"19:08", 17u) && drawClockLine(font, "clock-warm", "taskbar-clock-date",
		"10/17/2026", 18u);
	const bool clockUpdateOk = drawClockLine(font, "clock-update", "taskbar-clock-time",
		"19:09", 17u) && drawClockLine(font, "clock-update", "taskbar-clock-date",
		"10/18/2026", 18u);
	FontGlyphCacheStatistics stats = {};
	FontGlyphCacheGetStatistics(&stats);
	const bool pass = coldOk && warmOk && clockColdOk && clockWarmOk &&
		clockUpdateOk && s_failures == 0u;
	std::printf("FONT11 workload %s checks=%u failures=%u font_generation=%u final_hits=%llu final_misses=%llu final_rasters=%llu final_evictions=%llu entries=%u bytes=%u peak=%u entry_evictions=%llu coverage_evictions=%llu compactions=%llu bypasses=%llu oversized_bypasses=%llu admission_bypasses=%llu raster_failures=%llu\n",
		pass ? "PASS" : "FAIL", s_checks, s_failures, font.generation,
		static_cast<unsigned long long>(stats.hits),
		static_cast<unsigned long long>(stats.misses),
		static_cast<unsigned long long>(stats.rasterizations),
		static_cast<unsigned long long>(stats.evictions), stats.entryCount,
		stats.bytesUsed, stats.peakBytesUsed,
		static_cast<unsigned long long>(stats.entryCapacityEvictions),
		static_cast<unsigned long long>(stats.coverageCapacityEvictions),
		static_cast<unsigned long long>(stats.coverageCompactions),
		static_cast<unsigned long long>(stats.cacheBypasses),
		static_cast<unsigned long long>(stats.oversizedBypasses),
		static_cast<unsigned long long>(stats.admissionBypasses),
		static_cast<unsigned long long>(stats.rasterizationFailures));
	return pass ? 0 : 1;
}

namespace kernel {
namespace vfs {

Status stat(const char*, FileInfo*)
{
	return VFS_ERR_NOT_FOUND;
}

int32_t read_file(const char*, void*, uint32_t)
{
	return VFS_ERR_NOT_FOUND;
}

} // namespace vfs
} // namespace kernel
