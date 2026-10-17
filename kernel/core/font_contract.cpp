#include "include/kernel/font_contract.h"
#include "include/kernel/font_truetype.h"
#include "font_glyph_cache_internal.h"

namespace gxos {
namespace gui {
namespace {

static const uint16_t kInvalidSlot = 0xffffu;
static const UnicodeCodePoint kReplacementCodePoint = static_cast<UnicodeCodePoint>('?');

struct FontSlot {
	FontDefinition definition;
	uint32_t generation;
	uint8_t occupied;
};

FontSlot s_slots[kFontRegistryCapacity] = {};
uint32_t s_slotGenerations[kFontRegistryCapacity] = {};
uint8_t s_initialized = 0;
FontHandle s_defaultHandle = {kInvalidSlot, 0, 0};
FontHandle s_builtinHandle = {kInvalidSlot, 0, 0};
uint32_t s_managerGeneration = 0;
volatile uint32_t s_managerLock = 0;

void lockManager()
{
	while (__sync_lock_test_and_set(&s_managerLock, 1u)) {
		while (s_managerLock != 0) { }
	}
}

void unlockManager()
{
	__sync_lock_release(&s_managerLock);
}

struct ManagerGuard {
	ManagerGuard() { lockManager(); }
	~ManagerGuard() { unlockManager(); }
};

bool validScalar(UnicodeCodePoint cp)
{
	return cp <= 0x10ffffu && !(cp >= 0xd800u && cp <= 0xdfffu);
}

bool addU32(uint32_t left, uint32_t right, uint32_t* out)
{
	if (!out || right > 0xffffffffu - left) return false;
	*out = left + right;
	return true;
}

bool multiplyU32(uint32_t left, uint32_t right, uint32_t* out)
{
	if (!out || (left != 0 && right > 0xffffffffu / left)) return false;
	*out = left * right;
	return true;
}

void clearGlyph(FontGlyphView* out)
{
	if (!out) return;
	out->metrics.glyphId = 0;
	out->metrics.codePoint = 0;
	out->metrics.xBearingPx = 0;
	out->metrics.yBearingPx = 0;
	out->metrics.widthPx = 0;
	out->metrics.heightPx = 0;
	out->metrics.advancePx = 0;
	out->metrics.sideBearingPx = 0;
	out->bitmap = nullptr;
	out->bitmapByteLength = 0;
	out->bitmapFormat = FontBitmapFormat::None;
	out->fallbackUsed = 0;
	out->reserved[0] = 0;
	out->reserved[1] = 0;
	out->requestedCodePoint = 0;
}

FontStatus validateHandle(FontHandle handle, FontSlot** outSlot)
{
	if (!s_initialized) return FontStatus::NotInitialized;
	if (handle.slot >= kFontRegistryCapacity || handle.generation == 0)
		return FontStatus::InvalidHandle;
	FontSlot& slot = s_slots[handle.slot];
	if (!slot.occupied || slot.generation != handle.generation)
		return FontStatus::InvalidHandle;
	if (outSlot) *outSlot = &slot;
	return FontStatus::Ok;
}

FontStatus nextSlotGeneration(uint16_t slot)
{
	if (s_slotGenerations[slot] == 0xffffffffu) return FontStatus::GenerationExhausted;
	++s_slotGenerations[slot];
	if (s_slotGenerations[slot] == 0) return FontStatus::GenerationExhausted;
	return FontStatus::Ok;
}

FontStatus registerFontInternal(const FontDefinition* definition, FontHandle* outHandle)
{
	if (outHandle) {
		outHandle->slot = kInvalidSlot;
		outHandle->reserved = 0;
		outHandle->generation = 0;
	}
	if (!definition || !outHandle || definition->id.value == 0 || !definition->backend)
		return FontStatus::InvalidArgument;
	const uint32_t capabilities = definition->backend->capabilities;
	if ((capabilities & FontCapabilityMetadataOnly) != 0u) {
		if (capabilities != FontCapabilityMetadataOnly || definition->backend->getMetrics ||
			definition->backend->lookupGlyph || definition->backend->rasterizeGlyph ||
			!definition->trueTypeMetadata)
			return FontStatus::InvalidArgument;
	} else if (!definition->backend->getMetrics || !definition->backend->lookupGlyph ||
		(capabilities & (FontCapabilityGlyphLookup | FontCapabilityMeasurement)) !=
			(FontCapabilityGlyphLookup | FontCapabilityMeasurement)) {
		return FontStatus::InvalidArgument;
	}
	if ((capabilities & FontCapabilityRasterization) != 0u &&
		!definition->backend->rasterizeGlyph) return FontStatus::InvalidArgument;
	if ((capabilities & FontCapabilityRasterization) == 0u &&
		definition->backend->rasterizeGlyph) return FontStatus::InvalidArgument;
	for (uint16_t i = 0; i < kFontRegistryCapacity; ++i) {
		if (s_slots[i].occupied && s_slots[i].definition.id.value == definition->id.value)
			return FontStatus::DuplicateFontId;
	}
	for (uint16_t i = 0; i < kFontRegistryCapacity; ++i) {
		if (s_slots[i].occupied) continue;
		FontStatus generationStatus = nextSlotGeneration(i);
		if (generationStatus != FontStatus::Ok) return generationStatus;
		s_slots[i].definition = *definition;
		s_slots[i].generation = s_slotGenerations[i];
		s_slots[i].occupied = 1;
		outHandle->slot = i;
		outHandle->reserved = 0;
		outHandle->generation = s_slots[i].generation;
		return FontStatus::Ok;
	}
	return FontStatus::RegistryFull;
}

FontStatus decodeUtf8(const char* input, size_t length, size_t* offset,
	UnicodeCodePoint* outCodePoint)
{
	if (!input || !offset || !outCodePoint || *offset >= length)
		return FontStatus::InvalidArgument;
	const size_t start = *offset;
	const uint8_t first = static_cast<uint8_t>(input[start]);
	if (first <= 0x7fu) {
		*outCodePoint = first;
		*offset = start + 1u;
		return FontStatus::Ok;
	}
	uint32_t cp = 0;
	size_t count = 0;
	if (first >= 0xc2u && first <= 0xdfu) { cp = first & 0x1fu; count = 2; }
	else if (first >= 0xe0u && first <= 0xefu) { cp = first & 0x0fu; count = 3; }
	else if (first >= 0xf0u && first <= 0xf4u) { cp = first & 0x07u; count = 4; }
	else return FontStatus::InvalidUtf8;
	if (count > length - start) return FontStatus::InvalidUtf8;
	for (size_t i = 1; i < count; ++i) {
		const uint8_t next = static_cast<uint8_t>(input[start + i]);
		if ((next & 0xc0u) != 0x80u) return FontStatus::InvalidUtf8;
		if (i == 1) {
			if (first == 0xe0u && next < 0xa0u) return FontStatus::InvalidUtf8;
			if (first == 0xedu && next > 0x9fu) return FontStatus::InvalidUtf8;
			if (first == 0xf0u && next < 0x90u) return FontStatus::InvalidUtf8;
			if (first == 0xf4u && next > 0x8fu) return FontStatus::InvalidUtf8;
		}
		cp = (cp << 6) | (next & 0x3fu);
	}
	if (!validScalar(cp)) return FontStatus::InvalidUtf8;
	*outCodePoint = cp;
	*offset = start + count;
	return FontStatus::Ok;
}

FontStatus lookupGlyph(FontSlot* slot, FontSize size, UnicodeCodePoint cp,
	FontGlyphView* out, bool* usedFallback)
{
	if (!slot || !out) return FontStatus::InvalidArgument;
	clearGlyph(out);
	if ((slot->definition.backend->capabilities & FontCapabilityMetadataOnly) != 0u)
		return FontStatus::MetadataOnly;
	FontStatus status = slot->definition.backend->lookupGlyph(
		slot->definition.context, size, cp, out);
	if (status == FontStatus::Ok) {
		out->requestedCodePoint = cp;
		if (usedFallback) *usedFallback = false;
		return FontStatus::Ok;
	}
	if (status != FontStatus::UnsupportedCodePoint) return status;
	clearGlyph(out);
	status = slot->definition.backend->lookupGlyph(slot->definition.context,
		size, kReplacementCodePoint, out);
	if (status != FontStatus::Ok) return status == FontStatus::UnsupportedCodePoint ?
		FontStatus::BackendFailure : status;
	out->requestedCodePoint = cp;
	out->fallbackUsed = 1;
	if (usedFallback) *usedFallback = true;
	return FontStatus::FallbackUsed;
}

FontStatus getMetricsInternal(FontHandle handle, FontSize size, FontMetrics* outMetrics)
{
	if (!outMetrics) return FontStatus::InvalidArgument;
	*outMetrics = {};
	if (size.pixelHeight == 0) return FontStatus::InvalidArgument;
	FontSlot* slot = nullptr;
	const FontStatus status = validateHandle(handle, &slot);
	if (status != FontStatus::Ok) return status;
	if ((slot->definition.backend->capabilities & FontCapabilityMetadataOnly) != 0u)
		return FontStatus::MetadataOnly;
	FontMetrics metrics = {};
	const FontStatus backendStatus = slot->definition.backend->getMetrics(
		slot->definition.context, size, &metrics);
	if (backendStatus != FontStatus::Ok) return backendStatus;
	uint32_t verticalExtent = 0;
	if (metrics.lineHeightPx == 0 || metrics.baselinePx > metrics.lineHeightPx ||
		!addU32(metrics.ascentPx, metrics.descentPx, &verticalExtent) ||
		!addU32(verticalExtent, metrics.lineGapPx, &verticalExtent) ||
		verticalExtent > metrics.lineHeightPx)
		return FontStatus::BackendFailure;
	*outMetrics = metrics;
	return FontStatus::Ok;
}

FontStatus lookupGlyphInternal(FontHandle handle, FontSize size, UnicodeCodePoint codePoint,
	FontGlyphView* outGlyph)
{
	if (!outGlyph || !validScalar(codePoint)) return FontStatus::InvalidArgument;
	clearGlyph(outGlyph);
	FontSlot* slot = nullptr;
	const FontStatus handleStatus = validateHandle(handle, &slot);
	if (handleStatus != FontStatus::Ok) return handleStatus;
	FontMetrics metrics = {};
	const FontStatus metricsStatus = getMetricsInternal(handle, size, &metrics);
	if (metricsStatus != FontStatus::Ok) return metricsStatus;
	return lookupGlyph(slot, size, codePoint, outGlyph, nullptr);
}

FontStatus rasterizeGlyphForSlot(FontSlot* slot, FontSize size,
	UnicodeCodePoint codePoint, uint8_t* coverage, size_t coverageCapacity,
	uint32_t rowStrideBytes, GlyphMetrics* outMetrics)
{
	if (outMetrics) *outMetrics = {};
	if (!slot || !outMetrics || !validScalar(codePoint) || size.pixelHeight == 0u ||
		(!coverage && coverageCapacity != 0u)) return FontStatus::InvalidArgument;
	const FontBackend* backend = slot->definition.backend;
	const uint32_t capabilities = backend->capabilities;
	if ((capabilities & FontCapabilityRasterization) != 0u) {
		GlyphMetrics metrics = {};
		FontStatus status = backend->rasterizeGlyph(slot->definition.context, size,
			codePoint, coverage, coverageCapacity, rowStrideBytes, &metrics);
		if (status == FontStatus::UnsupportedCodePoint && codePoint != kReplacementCodePoint) {
			metrics = {};
			status = backend->rasterizeGlyph(slot->definition.context, size,
				kReplacementCodePoint, coverage, coverageCapacity, rowStrideBytes, &metrics);
			if (status == FontStatus::Ok) {
				metrics.codePoint = codePoint;
				*outMetrics = metrics;
				return FontStatus::FallbackUsed;
			}
		}
		if (status == FontStatus::Ok || status == FontStatus::FallbackUsed ||
			status == FontStatus::BufferTooSmall)
			*outMetrics = metrics;
		return status;
	}
	if ((capabilities & FontCapabilityBitmapData) == 0u)
		return (capabilities & FontCapabilityMetadataOnly) != 0u ?
			FontStatus::MetadataOnly : FontStatus::UnsupportedGlyphFeature;
	FontMetrics faceMetrics = {};
	const FontStatus faceStatus = backend->getMetrics(
		slot->definition.context, size, &faceMetrics);
	if (faceStatus != FontStatus::Ok) return faceStatus;
	FontGlyphView glyph = {};
	const FontStatus glyphStatus = lookupGlyph(slot, size, codePoint, &glyph, nullptr);
	if (glyphStatus != FontStatus::Ok && glyphStatus != FontStatus::FallbackUsed)
		return glyphStatus;
	const GlyphMetrics& metrics = glyph.metrics;
	if (metrics.widthPx > 128u || metrics.heightPx > 128u ||
		static_cast<uint64_t>(metrics.widthPx) * metrics.heightPx > 16384u)
		return FontStatus::RasterLimitExceeded;
	if (metrics.widthPx == 0u || metrics.heightPx == 0u) {
		*outMetrics = metrics;
		return glyphStatus;
	}
	if (metrics.heightPx > 8u) return FontStatus::BackendFailure;
	if (glyph.bitmapFormat != FontBitmapFormat::ColumnLsbTop || !glyph.bitmap ||
		glyph.bitmapByteLength < metrics.widthPx) return FontStatus::BackendFailure;
	if (rowStrideBytes < metrics.widthPx || rowStrideBytes > 4096u)
		return FontStatus::InvalidArgument;
	const size_t lastRow = static_cast<size_t>(metrics.heightPx - 1u) * rowStrideBytes;
	if (lastRow > static_cast<size_t>(-1) - metrics.widthPx)
		return FontStatus::ArithmeticOverflow;
	const size_t required = lastRow + metrics.widthPx;
	if (!coverage || coverageCapacity < required) {
		*outMetrics = metrics;
		return FontStatus::BufferTooSmall;
	}
	for (uint32_t y = 0; y < metrics.heightPx; ++y) {
		for (uint32_t x = 0; x < metrics.widthPx; ++x)
			coverage[static_cast<size_t>(y) * rowStrideBytes + x] =
				(glyph.bitmap[x] & (1u << y)) ? 255u : 0u;
	}
	*outMetrics = metrics;
	return glyphStatus;
}

struct CacheRasterRequest {
	FontSlot* slot;
	FontSize size;
	UnicodeCodePoint codePoint;
};

FontStatus rasterizeCacheMiss(void* context, uint8_t* coverage,
	size_t coverageCapacity, uint32_t rowStrideBytes, GlyphMetrics* outMetrics)
{
	CacheRasterRequest* request = static_cast<CacheRasterRequest*>(context);
	if (!request) return FontStatus::InvalidArgument;
	return rasterizeGlyphForSlot(request->slot, request->size, request->codePoint,
		coverage, coverageCapacity, rowStrideBytes, outMetrics);
}

uint32_t blendChannel(uint32_t foreground, uint32_t background, uint32_t alpha)
{
	return (foreground * alpha + background * (255u - alpha) + 127u) / 255u;
}

void compositeCoverage(uint32_t* pixels, uint32_t pitchWords,
	uint32_t width, uint32_t height, int64_t originX, int64_t originY,
	const GlyphMetrics& metrics, const uint8_t* coverage, uint32_t color)
{
	if (!pixels || !coverage || metrics.widthPx == 0u || metrics.heightPx == 0u) return;
	const uint32_t foregroundAlpha = (color >> 24) & 0xffu;
	const uint32_t foregroundRed = (color >> 16) & 0xffu;
	const uint32_t foregroundGreen = (color >> 8) & 0xffu;
	const uint32_t foregroundBlue = color & 0xffu;
	for (uint32_t row = 0; row < metrics.heightPx; ++row) {
		const int64_t destinationY = originY + row;
		if (destinationY < 0 || destinationY >= height) continue;
		for (uint32_t column = 0; column < metrics.widthPx; ++column) {
			const int64_t destinationX = originX + column;
			if (destinationX < 0 || destinationX >= width) continue;
			const uint32_t coverageValue = coverage[
				static_cast<size_t>(row) * metrics.widthPx + column];
			if (coverageValue == 0u || foregroundAlpha == 0u) continue;
			const uint32_t alpha = (coverageValue * foregroundAlpha + 127u) / 255u;
			if (alpha == 0u) continue;
			const size_t pixelIndex = static_cast<size_t>(destinationY) * pitchWords +
				static_cast<size_t>(destinationX);
			const uint32_t destination = pixels[pixelIndex];
			const uint32_t red = blendChannel(foregroundRed,
				(destination >> 16) & 0xffu, alpha);
			const uint32_t green = blendChannel(foregroundGreen,
				(destination >> 8) & 0xffu, alpha);
			const uint32_t blue = blendChannel(foregroundBlue, destination & 0xffu, alpha);
			// The selected GXApp surface is XRGB8888; its visible pixels are
			// opaque and the compositor forces alpha to 0xff on presentation.
			pixels[pixelIndex] = 0xff000000u | (red << 16) | (green << 8) | blue;
		}
	}
}

FontStatus measureTextInternal(FontHandle handle, FontSize size, const char* utf8,
	size_t byteLength, TextMeasureResult* outResult)
{
	if (!outResult) return FontStatus::InvalidArgument;
	*outResult = {};
	if ((!utf8 && byteLength != 0) || size.pixelHeight == 0) return FontStatus::InvalidArgument;
	if (byteLength > kFontMaxTextBytes) return FontStatus::InputTooLong;
	FontSlot* slot = nullptr;
	const FontStatus handleStatus = validateHandle(handle, &slot);
	if (handleStatus != FontStatus::Ok) return handleStatus;
	FontMetrics metrics = {};
	const FontStatus metricsStatus = getMetricsInternal(handle, size, &metrics);
	if (metricsStatus != FontStatus::Ok) return metricsStatus;
	TextMeasureResult result = {};
	result.byteLength = byteLength;
	result.lineCount = 1;
	bool anyFallback = false;
	bool previousWasCr = false;
	size_t offset = 0;
	uint32_t currentLineAdvance = 0;
	while (offset < byteLength) {
		const size_t start = offset;
		UnicodeCodePoint cp = 0;
		const FontStatus decodeStatus = decodeUtf8(utf8, byteLength, &offset, &cp);
		if (decodeStatus != FontStatus::Ok) {
			*outResult = {};
			outResult->errorByteOffset = start;
			return FontStatus::InvalidUtf8;
		}
		if (!addU32(result.codePointCount, 1u, &result.codePointCount))
			return FontStatus::ArithmeticOverflow;
		if (cp == '\r') {
			if (!addU32(result.lineCount, 1u, &result.lineCount)) return FontStatus::ArithmeticOverflow;
			if (currentLineAdvance > result.maxLineAdvancePx)
				result.maxLineAdvancePx = currentLineAdvance;
			currentLineAdvance = 0;
			previousWasCr = true;
			continue;
		}
		if (cp == '\n') {
			if (!previousWasCr && !addU32(result.lineCount, 1u, &result.lineCount))
				return FontStatus::ArithmeticOverflow;
			if (currentLineAdvance > result.maxLineAdvancePx)
				result.maxLineAdvancePx = currentLineAdvance;
			currentLineAdvance = 0;
			previousWasCr = false;
			continue;
		}
		previousWasCr = false;
		FontGlyphView glyph = {};
		bool usedFallback = false;
		const FontStatus glyphStatus = lookupGlyph(slot, size, cp, &glyph, &usedFallback);
		if (glyphStatus != FontStatus::Ok && glyphStatus != FontStatus::FallbackUsed)
			return glyphStatus;
		if (!addU32(result.glyphCount, 1u, &result.glyphCount) ||
			!addU32(currentLineAdvance, glyph.metrics.advancePx, &currentLineAdvance))
			return FontStatus::ArithmeticOverflow;
		if (usedFallback) {
			if (!addU32(result.fallbackGlyphCount, 1u, &result.fallbackGlyphCount))
				return FontStatus::ArithmeticOverflow;
			anyFallback = true;
		}
	}
	if (currentLineAdvance > result.maxLineAdvancePx)
		result.maxLineAdvancePx = currentLineAdvance;
	result.lastLineAdvancePx = currentLineAdvance;
	result.pixelWidth = result.maxLineAdvancePx;
	if (!multiplyU32(result.lineCount, metrics.lineHeightPx, &result.pixelHeight))
		return FontStatus::ArithmeticOverflow;
	*outResult = result;
	return anyFallback ? FontStatus::FallbackUsed : FontStatus::Ok;
}

} // namespace

FontStatus FontManagerInitialize()
{
	ManagerGuard guard;
	if (s_initialized) return FontStatus::Ok;
	internal::fontGlyphCacheResetLocked();
	s_initialized = 1;
	FontHandle builtin = {kInvalidSlot, 0, 0};
	const FontStatus status = registerFontInternal(FontBuiltinBitmapDefinition(), &builtin);
	if (status != FontStatus::Ok) {
		s_initialized = 0;
		return status;
	}
	s_builtinHandle = builtin;
	s_defaultHandle = builtin;
	if (s_managerGeneration == 0) s_managerGeneration = 1;
	return FontStatus::Ok;
}

void FontManagerShutdown()
{
	ManagerGuard guard;
	if (!s_initialized) return;
	internal::fontGlyphCacheResetLocked();
	for (uint16_t i = 0; i < kFontRegistryCapacity; ++i) {
		if (s_slots[i].occupied) {
			if (s_slots[i].definition.onUnregister)
				s_slots[i].definition.onUnregister(s_slots[i].definition.context);
			if (s_slotGenerations[i] != 0xffffffffu) ++s_slotGenerations[i];
		}
		s_slots[i].occupied = 0;
		s_slots[i].generation = 0;
		s_slots[i].definition.id.value = 0;
		s_slots[i].definition.debugName = nullptr;
		s_slots[i].definition.backend = nullptr;
		s_slots[i].definition.context = nullptr;
		s_slots[i].definition.trueTypeMetadata = nullptr;
		s_slots[i].definition.onUnregister = nullptr;
	}
	s_initialized = 0;
	s_defaultHandle.slot = kInvalidSlot;
	s_defaultHandle.reserved = 0;
	s_defaultHandle.generation = 0;
	s_builtinHandle.slot = kInvalidSlot;
	s_builtinHandle.reserved = 0;
	s_builtinHandle.generation = 0;
	if (s_managerGeneration != 0xffffffffu) ++s_managerGeneration;
}

bool FontManagerIsInitialized()
{
	ManagerGuard guard;
	return s_initialized != 0;
}

FontStatus FontManagerRegisterFont(const FontDefinition* definition, FontHandle* outHandle)
{
	ManagerGuard guard;
	if (outHandle) *outHandle = {kInvalidSlot, 0, 0};
	if (!s_initialized) return FontStatus::NotInitialized;
	return registerFontInternal(definition, outHandle);
}

FontStatus FontManagerUnloadFont(FontHandle handle)
{
	ManagerGuard guard;
	FontSlot* slot = nullptr;
	const FontStatus status = validateHandle(handle, &slot);
	if (status != FontStatus::Ok) return status;
	if (handle.slot == s_builtinHandle.slot && handle.generation == s_builtinHandle.generation)
		return FontStatus::CannotUnloadBuiltinFont;
	if (handle.slot == s_defaultHandle.slot && handle.generation == s_defaultHandle.generation)
		return FontStatus::FontInUse;
	internal::fontGlyphCacheInvalidateFontLocked(handle.slot, handle.generation);
	if (slot->definition.onUnregister)
		slot->definition.onUnregister(slot->definition.context);
	slot->occupied = 0;
	slot->generation = 0;
	slot->definition.id.value = 0;
	slot->definition.debugName = nullptr;
	slot->definition.backend = nullptr;
	slot->definition.context = nullptr;
	slot->definition.trueTypeMetadata = nullptr;
	slot->definition.onUnregister = nullptr;
	return FontStatus::Ok;
}

FontStatus FontManagerResolveFont(FontId id, FontHandle* outHandle)
{
	ManagerGuard guard;
	if (!outHandle) return FontStatus::InvalidArgument;
	*outHandle = {kInvalidSlot, 0, 0};
	if (!s_initialized) return FontStatus::NotInitialized;
	if (id.value == 0) {
		*outHandle = s_defaultHandle;
		return validateHandle(*outHandle, nullptr);
	}
	for (uint16_t i = 0; i < kFontRegistryCapacity; ++i) {
		if (s_slots[i].occupied && s_slots[i].definition.id.value == id.value) {
			outHandle->slot = i;
			outHandle->reserved = 0;
			outHandle->generation = s_slots[i].generation;
			return FontStatus::Ok;
		}
	}
	*outHandle = s_builtinHandle;
	return FontStatus::FallbackUsed;
}

FontStatus FontManagerGetDefaultFont(FontHandle* outHandle)
{
	ManagerGuard guard;
	if (!outHandle) return FontStatus::InvalidArgument;
	*outHandle = {kInvalidSlot, 0, 0};
	if (!s_initialized) return FontStatus::NotInitialized;
	*outHandle = s_defaultHandle;
	return validateHandle(*outHandle, nullptr);
}

FontStatus FontManagerSetDefaultFont(FontHandle handle)
{
	ManagerGuard guard;
	FontSlot* ignored = nullptr;
	const FontStatus status = validateHandle(handle, &ignored);
	if (status != FontStatus::Ok) return status;
	if ((ignored->definition.backend->capabilities & FontCapabilityMetadataOnly) != 0u)
		return FontStatus::MetadataOnly;
	if ((ignored->definition.backend->capabilities & FontCapabilityNoDefaultSelection) != 0u)
		return FontStatus::FontNotSelectable;
	if (handle.slot == s_defaultHandle.slot && handle.generation == s_defaultHandle.generation)
		return FontStatus::Ok;
	if (s_managerGeneration == 0xffffffffu) return FontStatus::GenerationExhausted;
	s_defaultHandle = handle;
	++s_managerGeneration;
	return FontStatus::Ok;
}

uint32_t FontManagerGeneration()
{
	ManagerGuard guard;
	return s_managerGeneration;
}

FontStatus FontManagerGetTrueTypeMetadata(FontHandle handle,
	FontTrueTypeMetadata* outMetadata)
{
	if (!outMetadata) return FontStatus::InvalidArgument;
	for (size_t i = 0; i < sizeof(*outMetadata); ++i)
		reinterpret_cast<uint8_t*>(outMetadata)[i] = 0;
	ManagerGuard guard;
	FontSlot* slot = nullptr;
	const FontStatus status = validateHandle(handle, &slot);
	if (status != FontStatus::Ok) return status;
	if (!slot->definition.trueTypeMetadata) return FontStatus::MetadataUnavailable;
	*outMetadata = *slot->definition.trueTypeMetadata;
	return FontStatus::Ok;
}

FontStatus FontUtf8CStringLength(const char* utf8, size_t* outByteLength)
{
	if (!outByteLength) return FontStatus::InvalidArgument;
	*outByteLength = 0;
	if (!utf8) return FontStatus::InvalidArgument;
	size_t length = 0;
	while (length < kFontMaxTextBytes && utf8[length] != '\0') ++length;
	if (length == kFontMaxTextBytes) return FontStatus::InputTooLong;
	*outByteLength = length;
	return FontStatus::Ok;
}

FontStatus FontGetMetrics(FontHandle handle, FontSize size, FontMetrics* outMetrics)
{
	ManagerGuard guard;
	return getMetricsInternal(handle, size, outMetrics);
}

FontStatus FontLookupGlyph(FontHandle handle, FontSize size, UnicodeCodePoint codePoint,
	FontGlyphView* outGlyph)
{
	ManagerGuard guard;
	return lookupGlyphInternal(handle, size, codePoint, outGlyph);
}

FontStatus FontRasterizeGlyph(FontHandle handle, FontSize size,
	UnicodeCodePoint codePoint, uint8_t* coverage, size_t coverageCapacity,
	uint32_t rowStrideBytes, GlyphMetrics* outMetrics)
{
	if (outMetrics) *outMetrics = {};
	if (!outMetrics || !validScalar(codePoint) || size.pixelHeight == 0 ||
		(!coverage && coverageCapacity != 0u)) return FontStatus::InvalidArgument;
	ManagerGuard guard;
	FontSlot* slot = nullptr;
	const FontStatus handleStatus = validateHandle(handle, &slot);
	if (handleStatus != FontStatus::Ok) return handleStatus;
	return rasterizeGlyphForSlot(slot, size, codePoint, coverage,
		coverageCapacity, rowStrideBytes, outMetrics);
}

FontStatus FontMeasureText(FontHandle handle, FontSize size, const char* utf8,
	size_t byteLength, TextMeasureResult* outResult)
{
	ManagerGuard guard;
	return measureTextInternal(handle, size, utf8, byteLength, outResult);
}

FontStatus FontDrawTextToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	int32_t x, int32_t y, const char* utf8, size_t byteLength, uint32_t color,
	TextMeasureResult* outMeasure)
{
	if (outMeasure) *outMeasure = {};
	if (!pixels || bufferWidth == 0 || bufferHeight == 0 || bufferWidth > 4096u ||
		bufferHeight > 4096u || pitchBytes < bufferWidth * 4u || (pitchBytes & 3u) != 0)
		return FontStatus::InvalidArgument;
	const uint64_t lastPixelIndex = static_cast<uint64_t>(bufferHeight - 1u) *
		(pitchBytes / 4u) + (bufferWidth - 1u);
	if (lastPixelIndex > static_cast<uint64_t>(static_cast<size_t>(-1)))
		return FontStatus::ArithmeticOverflow;
	ManagerGuard guard;
	TextMeasureResult measured = {};
	const FontStatus measureStatus = measureTextInternal(handle, size, utf8, byteLength, &measured);
	if (measureStatus != FontStatus::Ok && measureStatus != FontStatus::FallbackUsed) {
		if (outMeasure) *outMeasure = measured;
		return measureStatus;
	}
	FontMetrics fontMetrics = {};
	FontStatus status = getMetricsInternal(handle, size, &fontMetrics);
	if (status != FontStatus::Ok) return status;
	FontSlot* slot = nullptr;
	status = validateHandle(handle, &slot);
	if (status != FontStatus::Ok) return status;
	int64_t penX = x;
	int64_t penY = y;
	bool previousWasCr = false;
	size_t offset = 0;
	while (offset < byteLength) {
		UnicodeCodePoint cp = 0;
		status = decodeUtf8(utf8, byteLength, &offset, &cp);
		if (status != FontStatus::Ok) return status; // Preflight above guarantees this.
		if (cp == '\r') {
			penX = x;
			penY += fontMetrics.lineHeightPx;
			previousWasCr = true;
			continue;
		}
		if (cp == '\n') {
			if (!previousWasCr) penY += fontMetrics.lineHeightPx;
			penX = x;
			previousWasCr = false;
			continue;
		}
		previousWasCr = false;
		FontGlyphView glyph = {};
		status = lookupGlyph(slot, size, cp, &glyph, nullptr);
		if (status != FontStatus::Ok && status != FontStatus::FallbackUsed) return status;
		if (glyph.bitmapFormat != FontBitmapFormat::ColumnLsbTop || !glyph.bitmap ||
			glyph.metrics.heightPx > 8u ||
			glyph.bitmapByteLength < glyph.metrics.widthPx)
			return FontStatus::BackendFailure;
		for (uint32_t col = 0; col < glyph.metrics.widthPx; ++col) {
			const uint8_t bits = glyph.bitmap[col];
			for (uint32_t row = 0; row < glyph.metrics.heightPx && row < 8u; ++row) {
				if ((bits & (1u << row)) == 0) continue;
				const int64_t px = penX + glyph.metrics.xBearingPx + col;
				const int64_t py = penY + glyph.metrics.yBearingPx + row;
				if (px < 0 || py < 0 || px >= bufferWidth || py >= bufferHeight) continue;
				const uint64_t pixelIndex = static_cast<uint64_t>(py) *
					(pitchBytes / 4u) + static_cast<uint64_t>(px);
				pixels[static_cast<size_t>(pixelIndex)] = color;
			}
		}
		penX += glyph.metrics.advancePx;
	}
	if (outMeasure) *outMeasure = measured;
	return measureStatus;
}

FontStatus FontDrawGrayscaleTextToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, size_t destinationBytes, uint32_t pitchBytes,
	uint32_t bufferWidth, uint32_t bufferHeight, int32_t x, int32_t baselineY,
	const char* utf8, size_t byteLength, uint32_t color,
	TextMeasureResult* outMeasure)
{
	if (outMeasure) *outMeasure = {};
	if (!pixels || bufferWidth == 0u || bufferHeight == 0u ||
		bufferWidth > 4096u || bufferHeight > 4096u ||
		(!utf8 && byteLength != 0u)) return FontStatus::InvalidArgument;
	if (byteLength > kFontMaxTextBytes) return FontStatus::InputTooLong;
	const uint64_t minimumRowBytes = static_cast<uint64_t>(bufferWidth) * 4u;
	if (minimumRowBytes > 0xffffffffu || pitchBytes < minimumRowBytes ||
		(pitchBytes & 3u) != 0u) return FontStatus::InvalidArgument;
	const uint64_t requiredBytes = static_cast<uint64_t>(bufferHeight - 1u) * pitchBytes +
		minimumRowBytes;
	if (requiredBytes > static_cast<uint64_t>(static_cast<size_t>(-1)))
		return FontStatus::ArithmeticOverflow;
	if (destinationBytes < static_cast<size_t>(requiredBytes))
		return FontStatus::BufferTooSmall;

	ManagerGuard guard;
	TextMeasureResult measured = {};
	const FontStatus measureStatus = measureTextInternal(handle, size, utf8,
		byteLength, &measured);
	if (measureStatus != FontStatus::Ok && measureStatus != FontStatus::FallbackUsed) {
		if (outMeasure) *outMeasure = measured;
		return measureStatus;
	}
	FontMetrics faceMetrics = {};
	FontStatus status = getMetricsInternal(handle, size, &faceMetrics);
	if (status != FontStatus::Ok) return status;
	FontSlot* slot = nullptr;
	status = validateHandle(handle, &slot);
	if (status != FontStatus::Ok) return status;
	if (byteLength == 0u) {
		if (outMeasure) *outMeasure = measured;
		return measureStatus;
	}

	const uint32_t pitchWords = pitchBytes / sizeof(uint32_t);
	int64_t penX = x;
	int64_t penY = baselineY;
	bool previousWasCr = false;
	bool anyFallback = measureStatus == FontStatus::FallbackUsed;
	size_t offset = 0u;
	while (offset < byteLength) {
		UnicodeCodePoint codePoint = 0u;
		status = decodeUtf8(utf8, byteLength, &offset, &codePoint);
		if (status != FontStatus::Ok) return status; // measurement preflight validated UTF-8.
		if (codePoint == '\r') {
			penX = x;
			penY += faceMetrics.lineHeightPx;
			previousWasCr = true;
			continue;
		}
		if (codePoint == '\n') {
			if (!previousWasCr) penY += faceMetrics.lineHeightPx;
			penX = x;
			previousWasCr = false;
			continue;
		}
		previousWasCr = false;

		FontGlyphView glyph = {};
		bool lookupFallback = false;
		const FontStatus lookupStatus = lookupGlyph(slot, size, codePoint,
			&glyph, &lookupFallback);
		if (lookupStatus != FontStatus::Ok && lookupStatus != FontStatus::FallbackUsed)
			return lookupStatus;

		internal::FontGlyphCacheKey key = {};
		key.fontId = slot->definition.id;
		key.fontSlot = handle.slot;
		key.fontGeneration = handle.generation;
		key.glyphId = glyph.metrics.glyphId;
		key.pixelSize = size.pixelHeight;
		key.rasterConfig = internal::kFontGlyphRasterConfigGray8UnhintedV1;
		CacheRasterRequest request = {slot, size, codePoint};
		GlyphMetrics rasterMetrics = {};
		const uint8_t* coverage = nullptr;
		bool cacheHit = false;
		FontStatus rasterStatus = internal::fontGlyphCacheGetOrRasterize(key,
			glyph.metrics, &rasterizeCacheMiss, &request, &rasterMetrics,
			&coverage, &cacheHit);
		(void)cacheHit;

		bool usedRasterFallback = false;
		if (rasterStatus != FontStatus::Ok && rasterStatus != FontStatus::FallbackUsed) {
			// A failed scalable raster uses the immutable built-in question-mark
			// glyph for this character. The text's original advance is retained.
			if (handle.slot == s_builtinHandle.slot &&
				handle.generation == s_builtinHandle.generation) return rasterStatus;
			FontSlot* bitmapSlot = nullptr;
			const FontStatus bitmapHandleStatus = validateHandle(s_builtinHandle, &bitmapSlot);
			if (bitmapHandleStatus != FontStatus::Ok) return rasterStatus;
			FontGlyphView bitmapGlyph = {};
			const FontStatus bitmapLookupStatus = lookupGlyph(bitmapSlot,
				{kBitmapFontPixelHeight}, kReplacementCodePoint, &bitmapGlyph, nullptr);
			if (bitmapLookupStatus != FontStatus::Ok) return rasterStatus;
			internal::FontGlyphCacheKey bitmapKey = {};
			bitmapKey.fontId = bitmapSlot->definition.id;
			bitmapKey.fontSlot = s_builtinHandle.slot;
			bitmapKey.fontGeneration = s_builtinHandle.generation;
			bitmapKey.glyphId = bitmapGlyph.metrics.glyphId;
			bitmapKey.pixelSize = kBitmapFontPixelHeight;
			bitmapKey.rasterConfig = internal::kFontGlyphRasterConfigGray8UnhintedV1;
			CacheRasterRequest bitmapRequest = {bitmapSlot,
				{kBitmapFontPixelHeight}, kReplacementCodePoint};
			GlyphMetrics bitmapRasterMetrics = {};
			bool bitmapCacheHit = false;
			const FontStatus bitmapRasterStatus = internal::fontGlyphCacheGetOrRasterize(
				bitmapKey, bitmapGlyph.metrics, &rasterizeCacheMiss, &bitmapRequest,
				&bitmapRasterMetrics, &coverage, &bitmapCacheHit);
			(void)bitmapCacheHit;
			if (bitmapRasterStatus != FontStatus::Ok || !coverage) return rasterStatus;
			rasterMetrics = bitmapRasterMetrics;
			rasterMetrics.yBearingPx = static_cast<int32_t>(kBitmapFontPixelHeight);
			rasterMetrics.advancePx = glyph.metrics.advancePx;
			usedRasterFallback = true;
		}

		GlyphMetrics layoutMetrics = rasterMetrics;
		layoutMetrics.codePoint = codePoint;
		if ((slot->definition.backend->capabilities & FontCapabilityBitmapData) != 0u) {
			// FONT2 bitmap metrics use top-origin placement; convert them to this
			// API's shared baseline convention without changing the old renderer.
			layoutMetrics.yBearingPx = static_cast<int32_t>(faceMetrics.baselinePx);
		}
		if (usedRasterFallback) anyFallback = true;
		if (lookupFallback || lookupStatus == FontStatus::FallbackUsed ||
			rasterStatus == FontStatus::FallbackUsed) anyFallback = true;

		const int64_t glyphX = penX + layoutMetrics.xBearingPx;
		const int64_t glyphY = penY - layoutMetrics.yBearingPx;
		compositeCoverage(pixels, pitchWords, bufferWidth, bufferHeight,
			glyphX, glyphY, layoutMetrics, coverage, color);
		penX += glyph.metrics.advancePx;
	}
	if (outMeasure) *outMeasure = measured;
	return anyFallback ? FontStatus::FallbackUsed : FontStatus::Ok;
}

FontStatus FontDrawCStringToBuffer(FontHandle handle, FontSize size,
	uint32_t* pixels, uint32_t pitchBytes, uint32_t bufferWidth, uint32_t bufferHeight,
	int32_t x, int32_t y, const char* utf8, uint32_t color,
	TextMeasureResult* outMeasure)
{
	if (outMeasure) *outMeasure = {};
	size_t length = 0;
	const FontStatus lengthStatus = FontUtf8CStringLength(utf8, &length);
	if (lengthStatus != FontStatus::Ok) return lengthStatus;
	return FontDrawTextToBuffer(handle, size, pixels, pitchBytes, bufferWidth,
		bufferHeight, x, y, utf8, length, color, outMeasure);
}

FontStatus FontGlyphCacheGetStatistics(FontGlyphCacheStatistics* outStatistics)
{
	if (!outStatistics) return FontStatus::InvalidArgument;
	*outStatistics = {};
	ManagerGuard guard;
	internal::fontGlyphCacheGetStatisticsLocked(outStatistics);
	return FontStatus::Ok;
}

void FontGlyphCacheReset()
{
	ManagerGuard guard;
	internal::fontGlyphCacheResetLocked();
}

} // namespace gui
} // namespace gxos
