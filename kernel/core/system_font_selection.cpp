#include "include/kernel/system_font_selection.h"
#include "include/kernel/desktop.h"
#include "include/kernel/vfs.h"
#include "font_contract_internal.h"
#include "system_font_selection_internal.h"
#if defined(GXOS_FONT11_QEMU_PROOF)
#include "include/kernel/serial_debug.h"
#endif

namespace gxos {
namespace gui {
namespace {

static const char kFontDirectory[] = "/system/fonts";
static const char kPreferencePaths[2][16] = {"/fontsel0.cfg", "/fontsel1.cfg"};
static const char kPreferenceTempPath[] = "/fontsel.tmp";
static const uint32_t kPreferenceStorageBytes = 1024u;
static const uint32_t kPreferenceTextBytes = 2048u;
static const uint32_t kFontDirectoryPrefixBytes = 14u; // "/system/fonts/"
static const uint32_t kInvalidRecord = 0xffffffffu;

struct FontRecord {
	SystemFontIdentity identity;
	FontTrueTypeMetadata metadata;
	FontHandle handle;
	char actualPath[kernel::vfs::VFS_MAX_PATH];
	uint8_t occupied;
	uint8_t available;
	uint8_t reserved[2];
};

struct PreferenceRead {
	SystemFontConfiguredPreference preference;
	int32_t slot;
	uint8_t found;
	uint8_t unsupported;
	uint8_t corrupt;
	uint8_t reserved;
};

FontRecord s_records[kSystemFontMaxAvailableFonts] = {};
char s_directoryNames[kSystemFontMaxDiscoveredEntries][kernel::vfs::VFS_MAX_FILENAME] = {};
uint8_t s_fileScratch[kFontTrueTypeMaxFileBytes] = {};
uint8_t s_probeScratch[kFontTrueTypeMaxBitmapPixels] = {};
uint8_t s_previewScratch[kFontTrueTypeMaxBitmapPixels] = {};
char s_preferenceText[kPreferenceTextBytes] = {};
char s_preferenceSerialized[kPreferenceTextBytes] = {};
uint32_t s_recordCount = 0;
uint32_t s_catalogGeneration = 0;
uint32_t s_nextCandidateToken = 0;
uint32_t s_selectionGeneration = 1;
uint32_t s_activeRecord = kInvalidRecord;
uint32_t s_activeSize = kBitmapFontPixelHeight;
uint32_t s_previousRecord = kInvalidRecord;
uint32_t s_previousSize = kBitmapFontPixelHeight;
FontHandle s_builtinHandle = {0xffffu, 0u, 0u};
SystemFontConfiguredPreference s_configured = {};
SystemFontSelectionStatus s_preferenceStatus = SystemFontSelectionStatus::Ok;
SystemFontCandidate s_prepared = {};
uint32_t s_preparedRecord = kInvalidRecord;
uint8_t s_initialized = 0;
int8_t s_preferenceSlot = -1;
volatile uint32_t s_selectionLock = 0;

#if defined(GXOS_FONT11_QEMU_PROOF)
void font11r_log_stage(const char* stage)
{
	kernel::serial::puts("[FONT11R] stage=");
	kernel::serial::puts(stage);
	kernel::serial::puts("\n");
}

void font11r_log_result(const char* stage, int32_t result)
{
	kernel::serial::puts("[FONT11R] stage=");
	kernel::serial::puts(stage);
	kernel::serial::puts(" result=0x");
	kernel::serial::put_hex32(static_cast<uint32_t>(result));
	kernel::serial::puts("\n");
}

bool font11r_storage_read_write_probe()
{
	static const char kProbePath[] = "/f11r-rw.tmp";
	static const char kProbeBytes[] = "FONT11R-RW-PROBE-01";
	char readback[sizeof(kProbeBytes)] = {};
	font11r_log_stage("storage-probe-write-begin");
	const int32_t written = kernel::vfs::write_file(kProbePath, kProbeBytes,
		static_cast<uint32_t>(sizeof(kProbeBytes)));
	font11r_log_result("storage-probe-write", written);
	if (written != static_cast<int32_t>(sizeof(kProbeBytes))) return false;
	font11r_log_stage("storage-probe-read-begin");
	const int32_t read = kernel::vfs::read_file(kProbePath, readback,
		static_cast<uint32_t>(sizeof(readback)));
	font11r_log_result("storage-probe-read", read);
	bool matches = read == static_cast<int32_t>(sizeof(kProbeBytes));
	for (uint32_t i = 0u; matches && i < sizeof(kProbeBytes); ++i)
		matches = readback[i] == kProbeBytes[i];
	const kernel::vfs::Status removed = kernel::vfs::unlink(kProbePath);
	font11r_log_result("storage-probe-unlink", static_cast<int32_t>(removed));
	font11r_log_result("storage-probe-match", matches ? 1 : 0);
	return matches && removed == kernel::vfs::VFS_OK;
}
#define FONT11R_STAGE(stage) font11r_log_stage(stage)
#define FONT11R_RESULT(stage, result) font11r_log_result(stage, result)
#else
#define FONT11R_STAGE(stage) ((void)0)
#define FONT11R_RESULT(stage, result) ((void)0)
#endif

void lockSelection()
{
	while (__sync_lock_test_and_set(&s_selectionLock, 1u)) {
		while (s_selectionLock != 0u) { }
	}
}

void unlockSelection()
{
	__sync_lock_release(&s_selectionLock);
}

struct SelectionGuard {
	SelectionGuard() { lockSelection(); }
	~SelectionGuard() { unlockSelection(); }
};

uint32_t boundedLength(const char* text, uint32_t capacity)
{
	if (!text) return 0u;
	uint32_t length = 0u;
	while (length < capacity && text[length] != '\0') ++length;
	return length;
}

bool textEquals(const char* left, const char* right)
{
	if (!left || !right) return false;
	while (*left && *right && *left == *right) { ++left; ++right; }
	return *left == *right;
}

bool sliceEquals(const char* value, uint32_t length, const char* expected)
{
	if (!value || !expected) return false;
	uint32_t expectedLength = 0u;
	while (expected[expectedLength]) ++expectedLength;
	if (length != expectedLength) return false;
	for (uint32_t i = 0u; i < length; ++i)
		if (value[i] != expected[i]) return false;
	return true;
}

void copyText(char* destination, uint32_t capacity, const char* source)
{
	if (!destination || capacity == 0u) return;
	uint32_t i = 0u;
	if (source) while (i + 1u < capacity && source[i]) { destination[i] = source[i]; ++i; }
	destination[i] = '\0';
}

uint32_t blendPreviewChannel(uint32_t foreground, uint32_t background, uint32_t alpha)
{
	return (foreground * alpha + background * (255u - alpha) + 127u) / 255u;
}

bool decodePreviewUtf8(const char* input, size_t length, size_t* offset, uint32_t* outCodePoint)
{
	if (!input || !offset || !outCodePoint || *offset >= length) return false;
	const size_t start = *offset;
	const uint8_t first = static_cast<uint8_t>(input[start]);
	if (first <= 0x7fu) {
		*outCodePoint = first;
		*offset = start + 1u;
		return true;
	}
	uint32_t codePoint = 0u;
	size_t count = 0u;
	if (first >= 0xc2u && first <= 0xdfu) { codePoint = first & 0x1fu; count = 2u; }
	else if (first >= 0xe0u && first <= 0xefu) { codePoint = first & 0x0fu; count = 3u; }
	else if (first >= 0xf0u && first <= 0xf4u) { codePoint = first & 0x07u; count = 4u; }
	else return false;
	if (count > length - start) return false;
	for (size_t i = 1u; i < count; ++i) {
		const uint8_t next = static_cast<uint8_t>(input[start + i]);
		if ((next & 0xc0u) != 0x80u) return false;
		if (i == 1u && ((first == 0xe0u && next < 0xa0u) ||
			(first == 0xedu && next > 0x9fu) || (first == 0xf0u && next < 0x90u) ||
			(first == 0xf4u && next > 0x8fu))) return false;
		codePoint = (codePoint << 6) | (next & 0x3fu);
	}
	if (codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu)) return false;
	*outCodePoint = codePoint;
	*offset = start + count;
	return true;
}

void compositePreview(uint32_t* pixels, uint32_t pitchWords,
	uint32_t clipLeft, uint32_t clipTop, uint32_t clipRight, uint32_t clipBottom,
	int64_t originX, int64_t originY, const GlyphMetrics& metrics,
	const uint8_t* coverage, uint32_t color)
{
	if (!pixels || !coverage || metrics.widthPx == 0u || metrics.heightPx == 0u) return;
	const uint32_t foregroundAlpha = (color >> 24) & 0xffu;
	const uint32_t foregroundRed = (color >> 16) & 0xffu;
	const uint32_t foregroundGreen = (color >> 8) & 0xffu;
	const uint32_t foregroundBlue = color & 0xffu;
	for (uint32_t row = 0u; row < metrics.heightPx; ++row) {
		const int64_t destinationY = originY + row;
		if (destinationY < clipTop || destinationY >= clipBottom) continue;
		for (uint32_t column = 0u; column < metrics.widthPx; ++column) {
			const int64_t destinationX = originX + column;
			if (destinationX < clipLeft || destinationX >= clipRight) continue;
			const uint32_t coverageValue = coverage[static_cast<size_t>(row) * metrics.widthPx + column];
			if (coverageValue == 0u || foregroundAlpha == 0u) continue;
			const uint32_t alpha = (coverageValue * foregroundAlpha + 127u) / 255u;
			if (alpha == 0u) continue;
			const size_t pixelIndex = static_cast<size_t>(destinationY) * pitchWords +
				static_cast<size_t>(destinationX);
			const uint32_t destination = pixels[pixelIndex];
			const uint32_t red = blendPreviewChannel(foregroundRed, (destination >> 16) & 0xffu, alpha);
			const uint32_t green = blendPreviewChannel(foregroundGreen, (destination >> 8) & 0xffu, alpha);
			const uint32_t blue = blendPreviewChannel(foregroundBlue, destination & 0xffu, alpha);
			pixels[pixelIndex] = 0xff000000u | (red << 16) | (green << 8) | blue;
		}
	}
}

bool appendByte(char* output, uint32_t capacity, uint32_t* position, char value)
{
	if (!output || !position || *position >= capacity) return false;
	output[(*position)++] = value;
	return true;
}

bool appendText(char* output, uint32_t capacity, uint32_t* position, const char* value)
{
	if (!output || !position || !value) return false;
	for (uint32_t i = 0u; value[i]; ++i)
		if (!appendByte(output, capacity, position, value[i])) return false;
	return true;
}

uint32_t fnv1a32(const char* data, uint32_t length)
{
	uint32_t hash = 2166136261u;
	for (uint32_t i = 0u; i < length; ++i) {
		hash ^= static_cast<uint8_t>(data[i]);
		hash *= 16777619u;
	}
	return hash;
}

uint64_t fnv1a64(const uint8_t* data, uint32_t length, uint64_t seed = 14695981039346656037ull)
{
	uint64_t hash = seed;
	for (uint32_t i = 0u; i < length; ++i) {
		hash ^= data[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

uint8_t lowerAscii(uint8_t value)
{
	return value >= 'A' && value <= 'Z' ? static_cast<uint8_t>(value + ('a' - 'A')) : value;
}

int compareNames(const char* left, const char* right)
{
	uint32_t i = 0u;
	while (left[i] && right[i]) {
		const uint8_t a = lowerAscii(static_cast<uint8_t>(left[i]));
		const uint8_t b = lowerAscii(static_cast<uint8_t>(right[i]));
		if (a != b) return a < b ? -1 : 1;
		if (left[i] != right[i])
			return static_cast<uint8_t>(left[i]) < static_cast<uint8_t>(right[i]) ? -1 : 1;
		++i;
	}
	if (left[i] == right[i]) return 0;
	return left[i] ? 1 : -1;
}

void makeCanonicalPath(const char* filename, char* output, uint32_t capacity)
{
	if (!output || capacity == 0u) return;
	uint32_t position = 0u;
	const char prefix[] = "/system/fonts/";
	for (uint32_t i = 0u; prefix[i] && position + 1u < capacity; ++i)
		output[position++] = prefix[i];
	if (filename) {
		for (uint32_t i = 0u; filename[i] && position + 1u < capacity; ++i)
			output[position++] = filename[i];
	}
	output[position] = '\0';
}

bool hasTrueTypeExtension(const char* name)
{
	const uint32_t length = boundedLength(name, kernel::vfs::VFS_MAX_FILENAME);
	if (length < 5u || length == kernel::vfs::VFS_MAX_FILENAME) return false;
	return name[length - 4u] == '.' && lowerAscii(static_cast<uint8_t>(name[length - 3u])) == 't' &&
		lowerAscii(static_cast<uint8_t>(name[length - 2u])) == 't' &&
		lowerAscii(static_cast<uint8_t>(name[length - 1u])) == 'f';
}

bool hasTrueTypeExtensionBounded(const char* name, uint32_t capacity)
{
	const uint32_t length = boundedLength(name, capacity);
	if (length < 5u || length == capacity) return false;
	return name[length - 4u] == '.' && lowerAscii(static_cast<uint8_t>(name[length - 3u])) == 't' &&
		lowerAscii(static_cast<uint8_t>(name[length - 2u])) == 't' &&
		lowerAscii(static_cast<uint8_t>(name[length - 1u])) == 'f';
}

bool isSafeText(const char* text, uint32_t capacity, bool allowEmpty)
{
	const uint32_t length = boundedLength(text, capacity);
	if (length == capacity || (!allowEmpty && length == 0u)) return false;
	for (uint32_t i = 0u; i < length; ++i) {
		const uint8_t ch = static_cast<uint8_t>(text[i]);
		if (ch < 0x20u || ch == 0x7fu) return false;
	}
	return true;
}

uint64_t computeIdentityId(const SystemFontIdentity& identity)
{
	uint64_t hash = 14695981039346656037ull;
	const char domain[] = "guideXOS-font-identity-v1";
	hash = fnv1a64(reinterpret_cast<const uint8_t*>(domain), sizeof(domain) - 1u, hash);
	const uint32_t pathBytes = boundedLength(identity.canonicalPath, sizeof(identity.canonicalPath));
	const uint32_t familyBytes = boundedLength(identity.familyName, sizeof(identity.familyName));
	const uint32_t styleBytes = boundedLength(identity.styleName, sizeof(identity.styleName));
	hash = fnv1a64(reinterpret_cast<const uint8_t*>(identity.canonicalPath), pathBytes + 1u, hash);
	hash = fnv1a64(reinterpret_cast<const uint8_t*>(identity.familyName), familyBytes + 1u, hash);
	hash = fnv1a64(reinterpret_cast<const uint8_t*>(identity.styleName), styleBytes + 1u, hash);
	const uint8_t numeric[12] = {
		static_cast<uint8_t>(identity.fileBytes), static_cast<uint8_t>(identity.fileBytes >> 8),
		static_cast<uint8_t>(identity.fileBytes >> 16), static_cast<uint8_t>(identity.fileBytes >> 24),
		static_cast<uint8_t>(identity.contentFingerprint), static_cast<uint8_t>(identity.contentFingerprint >> 8),
		static_cast<uint8_t>(identity.contentFingerprint >> 16), static_cast<uint8_t>(identity.contentFingerprint >> 24),
		static_cast<uint8_t>(identity.contentFingerprint >> 32), static_cast<uint8_t>(identity.contentFingerprint >> 40),
		static_cast<uint8_t>(identity.contentFingerprint >> 48), static_cast<uint8_t>(identity.contentFingerprint >> 56),
	};
	hash = fnv1a64(numeric, sizeof(numeric), hash);
	if (hash == 0u || hash == kBuiltinBitmapFontId) hash ^= 0x9e3779b97f4a7c15ull;
	return hash;
}

bool identityEquals(const SystemFontIdentity& left, const SystemFontIdentity& right)
{
	return left.kind == right.kind && left.stableId == right.stableId &&
		left.fileBytes == right.fileBytes && left.contentFingerprint == right.contentFingerprint &&
		textEquals(left.canonicalPath, right.canonicalPath) &&
		textEquals(left.familyName, right.familyName) && textEquals(left.styleName, right.styleName);
}

void builtinIdentity(SystemFontIdentity* identity)
{
	if (!identity) return;
	for (uint32_t i = 0u; i < sizeof(*identity); ++i)
		reinterpret_cast<uint8_t*>(identity)[i] = 0u;
	copyText(identity->familyName, sizeof(identity->familyName), "guideXOS Bitmap");
	copyText(identity->styleName, sizeof(identity->styleName), "Regular");
	identity->stableId = kBuiltinBitmapFontId;
	identity->kind = SystemFontIdentityKind::BuiltinBitmap;
}

FontStatus readAndValidateFont(const char* path, uint32_t* outLength,
	FontTrueTypeMetadata* outMetadata, uint64_t* outFingerprint)
{
	if (outLength) *outLength = 0u;
	if (outMetadata) {
		for (uint32_t i = 0u; i < sizeof(*outMetadata); ++i)
			reinterpret_cast<uint8_t*>(outMetadata)[i] = 0u;
	}
	if (outFingerprint) *outFingerprint = 0u;
	if (!path || !outLength || !outMetadata || !outFingerprint) return FontStatus::InvalidArgument;
	kernel::vfs::FileInfo before = {};
	const kernel::vfs::Status statBefore = kernel::vfs::stat(path, &before);
	if (statBefore == kernel::vfs::VFS_ERR_NOT_FOUND) return FontStatus::FontFileNotFound;
	if (statBefore != kernel::vfs::VFS_OK || before.type != kernel::vfs::FILE_TYPE_REGULAR)
		return FontStatus::FontFileIoError;
	if (before.size == 0u) return FontStatus::InvalidFontFile;
	if (before.size > kFontTrueTypeMaxFileBytes) return FontStatus::FontFileTooLarge;
	const uint32_t length = static_cast<uint32_t>(before.size);
	const int32_t read = kernel::vfs::read_file(path, s_fileScratch, length);
	if (read < 0 || static_cast<uint32_t>(read) != length) return FontStatus::FontFileIoError;
	kernel::vfs::FileInfo after = {};
	if (kernel::vfs::stat(path, &after) != kernel::vfs::VFS_OK ||
		after.type != kernel::vfs::FILE_TYPE_REGULAR || after.size != before.size)
		return FontStatus::FontFileIoError;
	const FontTrueTypeStatus validation = FontTrueTypeValidate(s_fileScratch, length, outMetadata);
	if (validation != FontTrueTypeOk) {
		return validation == FontTrueTypeUnsupportedSignature ||
			validation == FontTrueTypeUnsupportedCollection ||
			validation == FontTrueTypeUnsupportedOutline ||
			validation == FontTrueTypeUnsupportedVariant ||
			validation == FontTrueTypeUnsupportedUnicodeMap
			? FontStatus::UnsupportedFontFormat : FontStatus::InvalidFontFile;
	}
	*outFingerprint = fnv1a64(s_fileScratch, length);
	if (*outFingerprint == 0u) *outFingerprint = 1u;
	*outLength = length;
	return FontStatus::Ok;
}

SystemFontSelectionStatus fontStatusToSelectionStatus(FontStatus status)
{
	switch (status) {
	case FontStatus::Ok:
	case FontStatus::FallbackUsed: return SystemFontSelectionStatus::Ok;
	case FontStatus::FontSourceCapacity: return SystemFontSelectionStatus::SourceCapacity;
	case FontStatus::RegistryFull: return SystemFontSelectionStatus::RegistryCapacity;
	case FontStatus::FontFileNotFound: return SystemFontSelectionStatus::NotFound;
	case FontStatus::UnsupportedFontFormat: return SystemFontSelectionStatus::UnsupportedFont;
	case FontStatus::InvalidFontFile:
	case FontStatus::FontFileTooLarge: return SystemFontSelectionStatus::InvalidFont;
	case FontStatus::GenerationExhausted: return SystemFontSelectionStatus::GenerationExhausted;
	default: return SystemFontSelectionStatus::PreparationFailed;
	}
}

int32_t findRecordById(uint64_t id)
{
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i)
		if (s_records[i].occupied && s_records[i].identity.stableId == id) return static_cast<int32_t>(i);
	return -1;
}

uint32_t findFreeRecord()
{
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i)
		if (!s_records[i].occupied) return i;
	return kInvalidRecord;
}

void clearRecord(FontRecord* record)
{
	if (!record) return;
	for (uint32_t i = 0u; i < sizeof(record->identity); ++i)
		reinterpret_cast<uint8_t*>(&record->identity)[i] = 0u;
	for (uint32_t i = 0u; i < sizeof(record->metadata); ++i)
		reinterpret_cast<uint8_t*>(&record->metadata)[i] = 0u;
	record->handle = {0xffffu, 0u, 0u};
	record->actualPath[0] = '\0';
	record->occupied = 0u;
	record->available = 0u;
}

bool validCanonicalFontPath(const char* path)
{
	if (!path || path[0] != '/') return false;
	const uint32_t length = boundedLength(path, kernel::vfs::VFS_MAX_PATH);
	if (length <= kFontDirectoryPrefixBytes || length >= kernel::vfs::VFS_MAX_PATH) return false;
	const char prefix[] = "/system/fonts/";
	for (uint32_t i = 0u; i < sizeof(prefix) - 1u; ++i)
		if (path[i] != prefix[i]) return false;
	if (!hasTrueTypeExtensionBounded(path, kernel::vfs::VFS_MAX_PATH)) return false;
	for (uint32_t i = kFontDirectoryPrefixBytes; i < length; ++i) {
		if (path[i] == '/' || path[i] == '\\' || path[i] == ':' || path[i] == '\n' || path[i] == '\r')
			return false;
	}
	return true;
}

bool appendHex32(char* output, uint32_t capacity, uint32_t* position, uint32_t value, uint32_t digits)
{
	static const char hex[] = "0123456789abcdef";
	for (uint32_t i = digits; i > 0u; --i) {
		const uint32_t shift = (i - 1u) * 4u;
		if (!appendByte(output, capacity, position, hex[(value >> shift) & 0xfu])) return false;
	}
	return true;
}

bool appendHex64(char* output, uint32_t capacity, uint32_t* position, uint64_t value)
{
	static const char hex[] = "0123456789abcdef";
	for (int32_t shift = 60; shift >= 0; shift -= 4)
		if (!appendByte(output, capacity, position, hex[(value >> shift) & 0xfu])) return false;
	return true;
}

bool appendHexText(char* output, uint32_t capacity, uint32_t* position, const char* text, uint32_t textCapacity)
{
	static const char hex[] = "0123456789abcdef";
	const uint32_t length = boundedLength(text, textCapacity);
	if (length == textCapacity) return false;
	for (uint32_t i = 0u; i < length; ++i) {
		const uint8_t value = static_cast<uint8_t>(text[i]);
		if (!appendByte(output, capacity, position, hex[value >> 4]) ||
			!appendByte(output, capacity, position, hex[value & 0xfu])) return false;
	}
	return true;
}

bool parseHexDigit(char value, uint8_t* out)
{
	if (!out) return false;
	if (value >= '0' && value <= '9') { *out = static_cast<uint8_t>(value - '0'); return true; }
	if (value >= 'a' && value <= 'f') { *out = static_cast<uint8_t>(value - 'a' + 10); return true; }
	if (value >= 'A' && value <= 'F') { *out = static_cast<uint8_t>(value - 'A' + 10); return true; }
	return false;
}

bool decodeHexText(const char* value, uint32_t length, char* output, uint32_t capacity)
{
	if (!value || !output || capacity == 0u || (length & 1u) != 0u || length / 2u >= capacity) return false;
	const uint32_t bytes = length / 2u;
	for (uint32_t i = 0u; i < bytes; ++i) {
		uint8_t high = 0u, low = 0u;
		if (!parseHexDigit(value[i * 2u], &high) || !parseHexDigit(value[i * 2u + 1u], &low)) return false;
		const uint8_t decoded = static_cast<uint8_t>((high << 4) | low);
		if (decoded < 0x20u || decoded == 0x7fu) return false;
		output[i] = static_cast<char>(decoded);
	}
	output[bytes] = '\0';
	return true;
}

bool parseDecimal(const char* value, uint32_t length, uint32_t* out)
{
	if (!value || !out || length == 0u) return false;
	uint32_t result = 0u;
	for (uint32_t i = 0u; i < length; ++i) {
		if (value[i] < '0' || value[i] > '9') return false;
		const uint32_t digit = static_cast<uint32_t>(value[i] - '0');
		if (result > (0xffffffffu - digit) / 10u) return false;
		result = result * 10u + digit;
	}
	*out = result;
	return true;
}

bool parseHex64(const char* value, uint32_t length, uint64_t* out)
{
	if (!value || !out || length != 16u) return false;
	uint64_t result = 0u;
	for (uint32_t i = 0u; i < length; ++i) {
		uint8_t digit = 0u;
		if (!parseHexDigit(value[i], &digit)) return false;
		result = (result << 4) | digit;
	}
	*out = result;
	return true;
}

bool parseHex32(const char* value, uint32_t length, uint32_t* out)
{
	if (!value || !out || length != 8u) return false;
	uint32_t result = 0u;
	for (uint32_t i = 0u; i < length; ++i) {
		uint8_t digit = 0u;
		if (!parseHexDigit(value[i], &digit)) return false;
		result = (result << 4) | digit;
	}
	*out = result;
	return true;
}

void defaultPreference(SystemFontConfiguredPreference* preference)
{
	if (!preference) return;
	for (uint32_t i = 0u; i < sizeof(*preference); ++i)
		reinterpret_cast<uint8_t*>(preference)[i] = 0u;
	builtinIdentity(&preference->identity);
	preference->pixelSize = kBitmapFontPixelHeight;
}

bool serializePreference(const SystemFontConfiguredPreference& preference,
	uint64_t sequence, char* output, uint32_t capacity)
{
	if (!output || capacity < kPreferenceStorageBytes || sequence == 0u) return false;
	for (uint32_t i = 0u; i < capacity; ++i) output[i] = ' ';
	uint32_t position = 0u;
	bool ok = appendText(output, capacity, &position, "format=1\nsequence=") &&
		appendHex64(output, capacity, &position, sequence) &&
		appendText(output, capacity, &position, "\nkind=");
	if (preference.identity.kind == SystemFontIdentityKind::BuiltinBitmap)
		ok = ok && appendText(output, capacity, &position, "bitmap");
	else
		ok = ok && appendText(output, capacity, &position, "truetype");
	ok = ok && appendText(output, capacity, &position, "\npixelSize=") &&
		appendHex32(output, capacity, &position, preference.pixelSize, 8u) &&
		appendText(output, capacity, &position, "\npathHex=") &&
		appendHexText(output, capacity, &position, preference.identity.canonicalPath,
			sizeof(preference.identity.canonicalPath)) &&
		appendText(output, capacity, &position, "\nfamilyHex=") &&
		appendHexText(output, capacity, &position, preference.identity.familyName,
			sizeof(preference.identity.familyName)) &&
		appendText(output, capacity, &position, "\nstyleHex=") &&
		appendHexText(output, capacity, &position, preference.identity.styleName,
			sizeof(preference.identity.styleName)) &&
		appendText(output, capacity, &position, "\nfileBytes=") &&
		appendHex32(output, capacity, &position, preference.identity.fileBytes, 8u) &&
		appendText(output, capacity, &position, "\nfingerprint=") &&
		appendHex64(output, capacity, &position, preference.identity.contentFingerprint) &&
		appendText(output, capacity, &position, "\nidentity=") &&
		appendHex64(output, capacity, &position, preference.identity.stableId) &&
		appendByte(output, capacity, &position, '\n');
	if (!ok || position + 18u >= capacity) return false;
	const uint32_t checksum = fnv1a32(output, position);
	ok = appendText(output, capacity, &position, "checksum=") &&
		appendHex32(output, capacity, &position, checksum, 8u) &&
		appendByte(output, capacity, &position, '\n');
	return ok && position < kPreferenceStorageBytes;
}

bool parsePreferenceText(char* text, uint32_t length,
	SystemFontConfiguredPreference* outPreference, bool* outUnsupported)
{
	if (outUnsupported) *outUnsupported = false;
	if (!text || !outPreference || length != kPreferenceStorageBytes) return false;
	uint32_t checksumOffset = 0xffffffffu;
	uint32_t expectedChecksum = 0u;
	uint32_t cursor = 0u;
	while (cursor < length) {
		const uint32_t start = cursor;
		while (cursor < length && text[cursor] != '\n') ++cursor;
		const uint32_t end = cursor;
		if (cursor < length) ++cursor;
		if (start == end) continue;
		bool spacesOnly = true;
		for (uint32_t i = start; i < end; ++i) {
			if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r') { spacesOnly = false; break; }
		}
		if (spacesOnly) continue;
		if (end - start >= 9u && sliceEquals(text + start, 9u, "checksum=")) {
			checksumOffset = start;
			if (!parseHex32(text + start + 9u, end - start - 9u, &expectedChecksum)) return false;
			for (uint32_t i = cursor; i < length; ++i)
				if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n') return false;
			break;
		}
	}
	if (checksumOffset == 0xffffffffu || fnv1a32(text, checksumOffset) != expectedChecksum) return false;

	SystemFontConfiguredPreference preference = {};
	defaultPreference(&preference);
	uint32_t format = 0u;
	uint32_t pixelSize = 0u;
	uint32_t fileBytes = 0u;
	uint64_t sequence = 0u, fingerprint = 0u, stableId = 0u;
	bool haveFormat = false, haveSequence = false, haveKind = false, haveSize = false;
	bool havePath = false, haveFamily = false, haveStyle = false, haveBytes = false;
	bool haveFingerprint = false, haveIdentity = false;
	cursor = 0u;
	while (cursor < checksumOffset) {
		const uint32_t start = cursor;
		while (cursor < checksumOffset && text[cursor] != '\n') ++cursor;
		const uint32_t end = cursor;
		if (cursor < checksumOffset) ++cursor;
		if (end == start) continue;
		uint32_t separator = start;
		while (separator < end && text[separator] != '=') ++separator;
		if (separator == end) return false;
		const char* key = text + start;
		const uint32_t keyLength = separator - start;
		const char* value = text + separator + 1u;
		const uint32_t valueLength = end - separator - 1u;
		if (keyLength == 6u && sliceEquals(key, keyLength, "format")) {
			if (haveFormat || !parseDecimal(value, valueLength, &format)) return false;
			haveFormat = true;
		} else if (keyLength == 8u && sliceEquals(key, keyLength, "sequence")) {
			if (haveSequence || !parseHex64(value, valueLength, &sequence)) return false;
			haveSequence = true;
		} else if (keyLength == 4u && sliceEquals(key, keyLength, "kind")) {
			if (haveKind) return false;
			if (valueLength == 6u && sliceEquals(value, valueLength, "bitmap"))
				preference.identity.kind = SystemFontIdentityKind::BuiltinBitmap;
			else if (valueLength == 8u && sliceEquals(value, valueLength, "truetype"))
				preference.identity.kind = SystemFontIdentityKind::TrueType;
			else return false;
			haveKind = true;
		} else if (keyLength == 9u && sliceEquals(key, keyLength, "pixelSize")) {
			if (haveSize || !parseHex32(value, valueLength, &pixelSize)) return false;
			haveSize = true;
		} else if (keyLength == 7u && sliceEquals(key, keyLength, "pathHex")) {
			if (havePath || !decodeHexText(value, valueLength, preference.identity.canonicalPath,
				sizeof(preference.identity.canonicalPath))) return false;
			havePath = true;
		} else if (keyLength == 9u && sliceEquals(key, keyLength, "familyHex")) {
			if (haveFamily || !decodeHexText(value, valueLength, preference.identity.familyName,
				sizeof(preference.identity.familyName))) return false;
			haveFamily = true;
		} else if (keyLength == 8u && sliceEquals(key, keyLength, "styleHex")) {
			if (haveStyle || !decodeHexText(value, valueLength, preference.identity.styleName,
				sizeof(preference.identity.styleName))) return false;
			haveStyle = true;
		} else if (keyLength == 9u && sliceEquals(key, keyLength, "fileBytes")) {
			if (haveBytes || !parseHex32(value, valueLength, &fileBytes)) return false;
			haveBytes = true;
		} else if (keyLength == 11u && sliceEquals(key, keyLength, "fingerprint")) {
			if (haveFingerprint || !parseHex64(value, valueLength, &fingerprint)) return false;
			haveFingerprint = true;
		} else if (keyLength == 8u && sliceEquals(key, keyLength, "identity")) {
			if (haveIdentity || !parseHex64(value, valueLength, &stableId)) return false;
			haveIdentity = true;
		} else {
			return false;
		}
	}
	if (!haveFormat) return false;
	if (format != kSystemFontPreferenceFormatVersion) {
		if (outUnsupported) *outUnsupported = true;
		return false;
	}
	if (!haveSequence || sequence == 0u || !haveKind || !haveSize || !havePath ||
		!haveFamily || !haveStyle || !haveBytes || !haveFingerprint || !haveIdentity) return false;
	preference.pixelSize = pixelSize;
	preference.sequence = sequence;
	preference.identity.fileBytes = fileBytes;
	preference.identity.contentFingerprint = fingerprint;
	preference.identity.stableId = stableId;
	if (preference.identity.kind == SystemFontIdentityKind::BuiltinBitmap) {
		SystemFontIdentity expected = {};
		builtinIdentity(&expected);
		if (pixelSize != kBitmapFontPixelHeight || preference.identity.fileBytes != 0u ||
			fingerprint != 0u || stableId != expected.stableId ||
			preference.identity.canonicalPath[0] != '\0' ||
			preference.identity.familyName[0] == '\0' || preference.identity.styleName[0] == '\0') return false;
		preference.identity.stableId = expected.stableId;
	} else {
		if (pixelSize < kSystemFontMinSelectablePixelSize || pixelSize > kSystemFontMaxSelectablePixelSize ||
			fileBytes == 0u || fileBytes > kFontTrueTypeMaxFileBytes || fingerprint == 0u || stableId == 0u ||
			!validCanonicalFontPath(preference.identity.canonicalPath) ||
			!isSafeText(preference.identity.familyName, sizeof(preference.identity.familyName), false) ||
			!isSafeText(preference.identity.styleName, sizeof(preference.identity.styleName), false) ||
			computeIdentityId(preference.identity) != stableId) return false;
	}
	preference.isPersisted = 1u;
	*outPreference = preference;
	return true;
}

PreferenceRead readPreferenceSlot(uint32_t slot)
{
	PreferenceRead result = {};
	result.slot = -1;
	if (slot > 1u) { result.corrupt = 1u; return result; }
	kernel::vfs::FileInfo info = {};
	const kernel::vfs::Status stat = kernel::vfs::stat(kPreferencePaths[slot], &info);
	if (stat == kernel::vfs::VFS_ERR_NOT_FOUND || stat == kernel::vfs::VFS_ERR_NOT_MOUNT) return result;
	result.found = 1u;
	if (stat != kernel::vfs::VFS_OK || info.type != kernel::vfs::FILE_TYPE_REGULAR ||
		info.size != kPreferenceStorageBytes) {
		result.corrupt = 1u;
		return result;
	}
	const int32_t count = kernel::vfs::read_file(kPreferencePaths[slot], s_preferenceText,
		kPreferenceTextBytes);
	if (count != static_cast<int32_t>(kPreferenceStorageBytes)) {
		result.corrupt = 1u;
		return result;
	}
	bool unsupported = false;
	if (!parsePreferenceText(s_preferenceText, static_cast<uint32_t>(count),
		&result.preference, &unsupported)) {
		result.unsupported = unsupported ? 1u : 0u;
		result.corrupt = unsupported ? 0u : 1u;
		return result;
	}
	result.slot = static_cast<int32_t>(slot);
	return result;
}

PreferenceRead readBestPreference()
{
	const PreferenceRead a = readPreferenceSlot(0u);
	const PreferenceRead b = readPreferenceSlot(1u);
	if (a.slot >= 0 && b.slot >= 0) {
		// Sequence numbers use serial-number arithmetic. The writer skips zero,
		// so UINT64_MAX -> 1 is an ordinary forward step across wraparound.
		const uint64_t distance = b.preference.sequence - a.preference.sequence;
		if (distance != 0u && distance < 0x8000000000000000ull) return b;
		return a;
	}
	if (a.slot >= 0) return a;
	if (b.slot >= 0) return b;
	PreferenceRead result = {};
	result.slot = -1;
	result.found = static_cast<uint8_t>(a.found || b.found);
	result.unsupported = static_cast<uint8_t>(a.unsupported || b.unsupported);
	result.corrupt = static_cast<uint8_t>(a.corrupt || b.corrupt);
	return result;
}

bool verifyPreferenceFile(const char* path, const char* serialized,
	const SystemFontConfiguredPreference& expected, uint64_t sequence)
{
	kernel::vfs::FileInfo info = {};
	const kernel::vfs::Status statStatus = kernel::vfs::stat(path, &info);
	FONT11R_RESULT("preference-verify-stat", static_cast<int32_t>(statStatus));
	if (statStatus != kernel::vfs::VFS_OK ||
		info.type != kernel::vfs::FILE_TYPE_REGULAR || info.size != kPreferenceStorageBytes)
		return false;
	const int32_t read = kernel::vfs::read_file(path, s_preferenceText, kPreferenceTextBytes);
	FONT11R_RESULT("preference-verify-read", read);
	if (read != static_cast<int32_t>(kPreferenceStorageBytes)) return false;
	for (uint32_t i = 0u; i < kPreferenceStorageBytes; ++i)
		if (s_preferenceText[i] != serialized[i]) return false;
	SystemFontConfiguredPreference verified = {};
	bool unsupported = false;
	const bool valid = parsePreferenceText(s_preferenceText, kPreferenceStorageBytes, &verified, &unsupported) &&
		identityEquals(verified.identity, expected.identity) &&
		verified.pixelSize == expected.pixelSize && verified.sequence == sequence;
	FONT11R_RESULT("preference-verify-record", valid ? 1 : 0);
	return valid;
}

void cleanupPreferenceScratch()
{
	// The fixed scratch path is never authoritative. If an interrupted attempt
	// left it behind, reclaim it best-effort; failure remains bounded to this
	// one known path and the next save can retry cleanup.
	const kernel::vfs::Status status = kernel::vfs::unlink(kPreferenceTempPath);
	(void)status;
}

bool persistPreference(const SystemFontConfiguredPreference& preference,
	uint32_t* outSlot, uint64_t* outSequence)
{
	const uint64_t sequence = s_configured.sequence == 0xffffffffffffffffull
		? 1u : (s_configured.sequence + 1u);
	const uint32_t targetSlot = s_preferenceSlot == 0 ? 1u : 0u;
	FONT11R_STAGE("preference-serialize-begin");
	const bool serialized = serializePreference(preference, sequence, s_preferenceSerialized,
		sizeof(s_preferenceSerialized));
	FONT11R_RESULT("preference-serialize", serialized ? 1 : 0);
	if (!serialized) return false;
	FONT11R_STAGE("preference-write-scratch-begin");
	int32_t written = kernel::vfs::write_file(kPreferenceTempPath, s_preferenceSerialized,
		kPreferenceStorageBytes);
	if (written != static_cast<int32_t>(kPreferenceStorageBytes)) {
		// Recreate only when a stale, undersized scratch file cannot hold this
		// fixed-size record. The scratch path is never authoritative.
		if (written == kernel::vfs::VFS_ERR_NO_SPACE) {
			const kernel::vfs::Status removeScratch = kernel::vfs::unlink(kPreferenceTempPath);
			if (removeScratch == kernel::vfs::VFS_OK || removeScratch == kernel::vfs::VFS_ERR_NOT_FOUND)
				written = kernel::vfs::create_file(kPreferenceTempPath,
					s_preferenceSerialized, kPreferenceStorageBytes);
		}
	}
	FONT11R_RESULT("preference-write-scratch", written);
	if (written != static_cast<int32_t>(kPreferenceStorageBytes)) return false;
	FONT11R_STAGE("preference-verify-scratch-begin");
	if (!verifyPreferenceFile(kPreferenceTempPath, s_preferenceSerialized, preference, sequence)) {
		FONT11R_RESULT("preference-verify-scratch", 0);
		return false;
	}
	FONT11R_RESULT("preference-verify-scratch", 1);

	// Publish by overwriting only the inactive slot. FAT's VFS rename path is
	// not consistently supported by folder-backed implementations. In-place
	// writes may leave this target incomplete, but the currently selected slot
	// remains untouched and is still the recovery record.
	const char* targetPath = kPreferencePaths[targetSlot];
	FONT11R_STAGE("preference-write-slot-begin");
	written = kernel::vfs::write_file(targetPath, s_preferenceSerialized,
		kPreferenceStorageBytes);
	if (written == kernel::vfs::VFS_ERR_NO_SPACE) {
		// A corrupt/short inactive slot may have too few allocated clusters for
		// an in-place rewrite. Reclaiming it is safe because the selected slot
		// remains intact; then create the replacement at the empty path.
		kernel::vfs::FileInfo targetInfo = {};
		const kernel::vfs::Status statStatus = kernel::vfs::stat(targetPath, &targetInfo);
		if (statStatus == kernel::vfs::VFS_OK &&
			kernel::vfs::unlink(targetPath) == kernel::vfs::VFS_OK)
			written = kernel::vfs::create_file(targetPath, s_preferenceSerialized,
				kPreferenceStorageBytes);
	}
	FONT11R_RESULT("preference-write-slot", written);
	if (written != static_cast<int32_t>(kPreferenceStorageBytes)) return false;
	FONT11R_STAGE("preference-verify-slot-begin");
	if (!verifyPreferenceFile(targetPath, s_preferenceSerialized, preference, sequence)) {
		FONT11R_RESULT("preference-verify-slot", 0);
		// The slot is inactive until verified. Best-effort removal prevents a
		// fully-written but unreadable candidate from superseding the prior
		// configured value after a restart.
		kernel::vfs::unlink(targetPath);
		return false;
	}
	FONT11R_RESULT("preference-verify-slot", 1);
	cleanupPreferenceScratch();
	FONT11R_STAGE("preference-cleanup-scratch-complete");
	if (outSlot) *outSlot = targetSlot;
	if (outSequence) *outSequence = sequence;
	return true;
}

SystemFontSelectionStatus loadAndApplyPreference()
{
	const PreferenceRead persisted = readBestPreference();
	cleanupPreferenceScratch();
	s_preferenceSlot = persisted.slot < 0 ? -1 : static_cast<int8_t>(persisted.slot);
	if (persisted.slot >= 0) {
		s_configured = persisted.preference;
		s_preferenceStatus = SystemFontSelectionStatus::Ok;
	} else {
		defaultPreference(&s_configured);
		if (persisted.unsupported) s_preferenceStatus = SystemFontSelectionStatus::PreferenceUnsupported;
		else if (persisted.corrupt) s_preferenceStatus = SystemFontSelectionStatus::PreferenceCorrupt;
		else s_preferenceStatus = SystemFontSelectionStatus::Ok;
	}
	s_activeRecord = kInvalidRecord;
	s_activeSize = kBitmapFontPixelHeight;
	internal::fontManagerSetSystemDefaultFont(s_builtinHandle);
	if (persisted.slot < 0 || persisted.preference.identity.kind == SystemFontIdentityKind::BuiltinBitmap)
		return s_preferenceStatus;
	const int32_t index = findRecordById(persisted.preference.identity.stableId);
	if (index < 0 || !s_records[index].available ||
		!identityEquals(persisted.preference.identity, s_records[index].identity)) {
		s_preferenceStatus = SystemFontSelectionStatus::NotFound;
		return SystemFontSelectionStatus::NotFound;
	}
	uint32_t length = 0u;
	FontTrueTypeMetadata metadata = {};
	uint64_t fingerprint = 0u;
	const FontStatus fileStatus = readAndValidateFont(s_records[index].actualPath,
		&length, &metadata, &fingerprint);
	if (fileStatus != FontStatus::Ok || length != persisted.preference.identity.fileBytes ||
		fingerprint != persisted.preference.identity.contentFingerprint) {
		s_preferenceStatus = fileStatus == FontStatus::FontFileNotFound
			? SystemFontSelectionStatus::NotFound : SystemFontSelectionStatus::StaleIdentity;
		return s_preferenceStatus;
	}
	FontMetrics metrics = {};
	const FontStatus metricsStatus = FontGetMetrics(s_records[index].handle,
		{persisted.preference.pixelSize}, &metrics);
	if (metricsStatus != FontStatus::Ok || metrics.lineHeightPx == 0u) {
		s_preferenceStatus = SystemFontSelectionStatus::PreparationFailed;
		return s_preferenceStatus;
	}
	const FontStatus selectStatus = internal::fontManagerSetSystemDefaultFont(s_records[index].handle);
	if (selectStatus != FontStatus::Ok) {
		s_preferenceStatus = fontStatusToSelectionStatus(selectStatus);
		return s_preferenceStatus;
	}
	s_activeRecord = static_cast<uint32_t>(index);
	s_activeSize = persisted.preference.pixelSize;
	if (s_selectionGeneration != 0xffffffffu) ++s_selectionGeneration;
	kernel::desktop::request_redraw();
	return SystemFontSelectionStatus::Ok;
}

SystemFontSelectionStatus refreshCatalogLocked()
{
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	uint8_t directory = kernel::vfs::opendir(kFontDirectory);
	if (directory == 0xffu) return SystemFontSelectionStatus::DirectoryUnavailable;
	uint32_t nameCount = 0u;
	uint32_t scannedEntries = 0u;
	kernel::vfs::DirEntry entry = {};
	while (scannedEntries < kSystemFontMaxDiscoveredEntries &&
		kernel::vfs::readdir(directory, &entry)) {
		++scannedEntries;
		if (entry.type != kernel::vfs::FILE_TYPE_REGULAR || !hasTrueTypeExtension(entry.name)) continue;
		copyText(s_directoryNames[nameCount], sizeof(s_directoryNames[nameCount]), entry.name);
		if (s_directoryNames[nameCount][0]) ++nameCount;
	}
	const bool limitReached = scannedEntries == kSystemFontMaxDiscoveredEntries &&
		kernel::vfs::readdir(directory, &entry);
	kernel::vfs::closedir(directory);
	for (uint32_t i = 1u; i < nameCount; ++i) {
		char name[kernel::vfs::VFS_MAX_FILENAME];
		copyText(name, sizeof(name), s_directoryNames[i]);
		uint32_t j = i;
		while (j > 0u && compareNames(name, s_directoryNames[j - 1u]) < 0) {
			copyText(s_directoryNames[j], sizeof(s_directoryNames[j]), s_directoryNames[j - 1u]);
			--j;
		}
		copyText(s_directoryNames[j], sizeof(s_directoryNames[j]), name);
	}
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i)
		if (s_records[i].occupied) s_records[i].available = 0u;
	SystemFontSelectionStatus firstFailure = SystemFontSelectionStatus::Ok;
	uint32_t accepted = 0u;
	for (uint32_t n = 0u; n < nameCount; ++n) {
		char actualPath[kernel::vfs::VFS_MAX_PATH] = {};
		uint32_t pathPosition = 0u;
		if (!appendText(actualPath, sizeof(actualPath), &pathPosition, kFontDirectory) ||
			!appendByte(actualPath, sizeof(actualPath), &pathPosition, '/') ||
			!appendText(actualPath, sizeof(actualPath), &pathPosition, s_directoryNames[n]) ||
			!appendByte(actualPath, sizeof(actualPath), &pathPosition, '\0')) continue;
		SystemFontIdentity identity = {};
		makeCanonicalPath(s_directoryNames[n], identity.canonicalPath,
			sizeof(identity.canonicalPath));
		FontTrueTypeMetadata metadata = {};
		uint32_t length = 0u;
		uint64_t fingerprint = 0u;
		const FontStatus fileStatus = readAndValidateFont(actualPath, &length, &metadata, &fingerprint);
		if (fileStatus != FontStatus::Ok) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = fontStatusToSelectionStatus(fileStatus);
			continue;
		}
		identity.fileBytes = length;
		identity.contentFingerprint = fingerprint;
		identity.kind = SystemFontIdentityKind::TrueType;
		copyText(identity.familyName, sizeof(identity.familyName), metadata.familyName);
		copyText(identity.styleName, sizeof(identity.styleName), metadata.styleName);
		if (!isSafeText(identity.familyName, sizeof(identity.familyName), false) ||
			!isSafeText(identity.styleName, sizeof(identity.styleName), false)) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = SystemFontSelectionStatus::InvalidFont;
			continue;
		}
		identity.stableId = computeIdentityId(identity);
		const int32_t existing = findRecordById(identity.stableId);
		if (existing >= 0 && identityEquals(identity, s_records[existing].identity)) {
			FontMetrics metrics = {};
			if (FontGetMetrics(s_records[existing].handle, {16u}, &metrics) == FontStatus::Ok) {
				s_records[existing].available = 1u;
				++accepted;
				continue;
			}
			clearRecord(&s_records[existing]);
			if (s_recordCount) --s_recordCount;
		}
		bool pathConflict = false;
		for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i) {
			if (!s_records[i].occupied || !textEquals(s_records[i].identity.canonicalPath,
				identity.canonicalPath)) continue;
			pathConflict = true;
			if (i == s_activeRecord) break;
			const FontStatus unload = FontManagerUnloadFont(s_records[i].handle);
			if (unload == FontStatus::Ok) {
				clearRecord(&s_records[i]);
				if (s_recordCount) --s_recordCount;
				pathConflict = false;
			}
			break;
		}
		if (pathConflict) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = SystemFontSelectionStatus::StaleIdentity;
			continue;
		}
		if (accepted >= kSystemFontMaxAvailableFonts) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = SystemFontSelectionStatus::RegistryCapacity;
			continue;
		}
		const uint32_t slot = findFreeRecord();
		if (slot == kInvalidRecord) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = SystemFontSelectionStatus::RegistryCapacity;
			continue;
		}
		FontHandle handle = {0xffffu, 0u, 0u};
		FontTrueTypeMetadata registeredMetadata = {};
		const FontStatus loadStatus = FontTrueTypeLoadMemory({identity.stableId}, s_fileScratch,
			length, &handle, &registeredMetadata);
		if (loadStatus != FontStatus::Ok) {
			if (firstFailure == SystemFontSelectionStatus::Ok)
				firstFailure = fontStatusToSelectionStatus(loadStatus);
			continue;
		}
		FontRecord& record = s_records[slot];
		record.identity = identity;
		record.metadata = registeredMetadata;
		record.handle = handle;
		copyText(record.actualPath, sizeof(record.actualPath), actualPath);
		record.occupied = 1u;
		record.available = 1u;
		++s_recordCount;
		++accepted;
	}
	// Entries removed from disk stay pinned only while they are the active face.
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i) {
		if (!s_records[i].occupied || s_records[i].available || i == s_activeRecord) continue;
		if (FontManagerUnloadFont(s_records[i].handle) == FontStatus::Ok) {
			clearRecord(&s_records[i]);
			if (s_recordCount) --s_recordCount;
		}
	}
	if (s_catalogGeneration != 0xffffffffu) ++s_catalogGeneration;
	if (limitReached) return SystemFontSelectionStatus::DiscoveryLimitReached;
	return firstFailure;
}

SystemFontSelectionStatus prepareRecord(uint32_t recordIndex, uint32_t pixelSize,
	SystemFontCandidate* outCandidate)
{
	if (!outCandidate || recordIndex >= kSystemFontMaxAvailableFonts ||
		!s_records[recordIndex].occupied || !s_records[recordIndex].available)
		return SystemFontSelectionStatus::NotFound;
	if (pixelSize < kSystemFontMinSelectablePixelSize ||
		pixelSize > kSystemFontMaxSelectablePixelSize)
		return SystemFontSelectionStatus::SizeOutOfRange;
	FontRecord& record = s_records[recordIndex];
	uint32_t length = 0u;
	FontTrueTypeMetadata metadata = {};
	uint64_t fingerprint = 0u;
	const FontStatus fileStatus = readAndValidateFont(record.actualPath,
		&length, &metadata, &fingerprint);
	if (fileStatus == FontStatus::FontFileNotFound)
		return SystemFontSelectionStatus::NotFound;
	if (fileStatus != FontStatus::Ok) return fontStatusToSelectionStatus(fileStatus);
	if (length != record.identity.fileBytes || fingerprint != record.identity.contentFingerprint ||
		!textEquals(metadata.familyName, record.metadata.familyName) ||
		!textEquals(metadata.styleName, record.metadata.styleName))
		return SystemFontSelectionStatus::StaleIdentity;
	FontMetrics metrics = {};
	if (FontGetMetrics(record.handle, {pixelSize}, &metrics) != FontStatus::Ok ||
		metrics.lineHeightPx == 0u || metrics.lineHeightPx > 256u)
		return SystemFontSelectionStatus::PreparationFailed;
	TextMeasureResult measured = {};
	if (FontMeasureText(record.handle, {pixelSize}, "Ag", 2u, &measured) != FontStatus::Ok ||
		measured.glyphCount != 2u || measured.pixelWidth == 0u)
		return SystemFontSelectionStatus::PreparationFailed;
	FontGlyphView glyph = {};
	if (FontLookupGlyph(record.handle, {pixelSize}, 'A', &glyph) != FontStatus::Ok ||
		glyph.metrics.widthPx > kFontTrueTypeMaxBitmapDimension ||
		glyph.metrics.heightPx > kFontTrueTypeMaxBitmapDimension)
		return SystemFontSelectionStatus::PreparationFailed;
	FontStatus raster = FontRasterizeGlyph(record.handle, {pixelSize}, 'A', s_probeScratch,
		sizeof(s_probeScratch), glyph.metrics.widthPx, &glyph.metrics);
	if (raster != FontStatus::Ok && raster != FontStatus::FallbackUsed)
		return SystemFontSelectionStatus::PreparationFailed;
	if (s_nextCandidateToken == 0xffffffffu) s_nextCandidateToken = 0u;
	++s_nextCandidateToken;
	if (s_nextCandidateToken == 0u) ++s_nextCandidateToken;
	s_prepared = {record.identity.stableId, s_catalogGeneration,
		s_nextCandidateToken, pixelSize};
	s_preparedRecord = recordIndex;
	*outCandidate = s_prepared;
	return SystemFontSelectionStatus::Ok;
}

SystemFontSelectionStatus verifyRecordSource(uint32_t recordIndex)
{
	if (recordIndex >= kSystemFontMaxAvailableFonts || !s_records[recordIndex].occupied ||
		!s_records[recordIndex].available) return SystemFontSelectionStatus::NotFound;
	FontRecord& record = s_records[recordIndex];
	uint32_t length = 0u;
	FontTrueTypeMetadata metadata = {};
	uint64_t fingerprint = 0u;
	const FontStatus fileStatus = readAndValidateFont(record.actualPath,
		&length, &metadata, &fingerprint);
	if (fileStatus == FontStatus::FontFileNotFound) return SystemFontSelectionStatus::NotFound;
	if (fileStatus != FontStatus::Ok) return fontStatusToSelectionStatus(fileStatus);
	return length == record.identity.fileBytes &&
		fingerprint == record.identity.contentFingerprint &&
		textEquals(metadata.familyName, record.metadata.familyName) &&
		textEquals(metadata.styleName, record.metadata.styleName)
		? SystemFontSelectionStatus::Ok : SystemFontSelectionStatus::StaleIdentity;
}

SystemFontSelectionStatus applyRecord(uint32_t recordIndex, uint32_t pixelSize,
	bool persist, bool verifyFile)
{
	const bool builtin = recordIndex == kInvalidRecord;
	if (!builtin && (recordIndex >= kSystemFontMaxAvailableFonts ||
		!s_records[recordIndex].occupied || !s_records[recordIndex].available))
		return SystemFontSelectionStatus::NotFound;
	if (!builtin && (pixelSize < kSystemFontMinSelectablePixelSize ||
		pixelSize > kSystemFontMaxSelectablePixelSize))
		return SystemFontSelectionStatus::SizeOutOfRange;
	if (builtin && pixelSize != kBitmapFontPixelHeight)
		return SystemFontSelectionStatus::SizeOutOfRange;
	if (s_selectionGeneration == 0xffffffffu)
		return SystemFontSelectionStatus::GenerationExhausted;
	FontHandle targetHandle = builtin ? s_builtinHandle : s_records[recordIndex].handle;
	if (verifyFile && !builtin) {
		FONT11R_STAGE("candidate-source-validation-begin");
		const SystemFontSelectionStatus verified = verifyRecordSource(recordIndex);
		FONT11R_RESULT("candidate-source-validation", static_cast<int32_t>(verified));
		if (verified != SystemFontSelectionStatus::Ok) return verified;
	}
	FontHandle oldHandle = s_builtinHandle;
	FontManagerGetDefaultFont(&oldHandle);
	const uint32_t managerGeneration = FontManagerGeneration();
	if ((oldHandle.slot != targetHandle.slot || oldHandle.generation != targetHandle.generation) &&
		managerGeneration >= 0xfffffffeu)
		return SystemFontSelectionStatus::GenerationExhausted;
	const uint32_t oldRecord = s_activeRecord;
	const uint32_t oldSize = s_activeSize;
	const bool selectionChanged = oldRecord != recordIndex || oldSize != pixelSize;
	if (!selectionChanged && persist && s_configured.isPersisted &&
		s_configured.pixelSize == pixelSize &&
		((builtin && s_configured.identity.kind == SystemFontIdentityKind::BuiltinBitmap) ||
		(!builtin && identityEquals(s_configured.identity, s_records[recordIndex].identity))))
		return SystemFontSelectionStatus::Ok;
	FONT11R_STAGE("active-font-handle-stage-begin");
	const FontStatus setStatus = internal::fontManagerSetSystemDefaultFont(targetHandle);
	FONT11R_RESULT("active-font-handle-stage", static_cast<int32_t>(setStatus));
	if (setStatus != FontStatus::Ok) return fontStatusToSelectionStatus(setStatus);
	if (persist) {
		SystemFontConfiguredPreference desired = {};
		defaultPreference(&desired);
		if (!builtin) desired.identity = s_records[recordIndex].identity;
		desired.pixelSize = pixelSize;
		uint32_t savedSlot = 0u;
		uint64_t savedSequence = 0u;
		if (!persistPreference(desired, &savedSlot, &savedSequence)) {
			if (oldHandle.slot != targetHandle.slot || oldHandle.generation != targetHandle.generation) {
				if (internal::fontManagerSetSystemDefaultFont(oldHandle) != FontStatus::Ok)
					return SystemFontSelectionStatus::RollbackFailed;
			}
			return SystemFontSelectionStatus::PersistenceFailed;
		}
		desired.isPersisted = 1u;
		desired.sequence = savedSequence;
		s_configured = desired;
		s_preferenceSlot = static_cast<int8_t>(savedSlot);
		s_preferenceStatus = SystemFontSelectionStatus::Ok;
	}
	if (selectionChanged) {
		s_previousRecord = oldRecord;
		s_previousSize = oldSize;
		++s_selectionGeneration;
	}
	s_activeRecord = recordIndex;
	s_activeSize = pixelSize;
	FONT11R_RESULT("active-selection-published", static_cast<int32_t>(SystemFontSelectionStatus::Ok));
	return SystemFontSelectionStatus::Ok;
}

void notifyDesktop()
{
	kernel::desktop::request_redraw();
}

} // namespace

SystemFontSelectionStatus SystemFontSelectionInitialize()
{
	SelectionGuard guard;
	if (s_initialized) return SystemFontSelectionStatus::Ok;
	const FontStatus managerStatus = FontManagerInitialize();
	if (managerStatus != FontStatus::Ok) return fontStatusToSelectionStatus(managerStatus);
	if (FontManagerGetDefaultFont(&s_builtinHandle) != FontStatus::Ok)
		return SystemFontSelectionStatus::PreparationFailed;
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i) clearRecord(&s_records[i]);
	s_recordCount = 0u;
	s_activeRecord = kInvalidRecord;
	s_activeSize = kBitmapFontPixelHeight;
	s_previousRecord = kInvalidRecord;
	s_previousSize = kBitmapFontPixelHeight;
	s_configured = {};
	defaultPreference(&s_configured);
	s_preferenceSlot = -1;
	s_preferenceStatus = SystemFontSelectionStatus::Ok;
	s_initialized = 1u;
	(void)refreshCatalogLocked();
	const SystemFontSelectionStatus restored = loadAndApplyPreference();
#if defined(GXOS_FONT11_QEMU_PROOF)
	const bool storageProbePassed = font11r_storage_read_write_probe();
	font11r_log_result("storage-probe", storageProbePassed ? 1 : 0);
#endif
	return restored == SystemFontSelectionStatus::PreferenceCorrupt ||
		restored == SystemFontSelectionStatus::PreferenceUnsupported ||
		restored == SystemFontSelectionStatus::NotFound ||
		restored == SystemFontSelectionStatus::StaleIdentity ||
		restored == SystemFontSelectionStatus::PreparationFailed
		? SystemFontSelectionStatus::Ok : restored;
}

void SystemFontSelectionShutdown()
{
	SelectionGuard guard;
	if (!s_initialized) return;
	FontManagerShutdown();
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i) clearRecord(&s_records[i]);
	s_recordCount = 0u;
	s_activeRecord = kInvalidRecord;
	s_activeSize = kBitmapFontPixelHeight;
	s_previousRecord = kInvalidRecord;
	s_previousSize = kBitmapFontPixelHeight;
	s_prepared = {};
	s_preparedRecord = kInvalidRecord;
	s_catalogGeneration = 0u;
	s_nextCandidateToken = 0u;
	s_selectionGeneration = 1u;
	s_builtinHandle = {0xffffu, 0u, 0u};
	s_configured = {};
	s_preferenceStatus = SystemFontSelectionStatus::Ok;
	s_preferenceSlot = -1;
	s_initialized = 0u;
}

SystemFontSelectionStatus SystemFontSelectionRefreshCatalog()
{
	SelectionGuard guard;
	return refreshCatalogLocked();
}

SystemFontSelectionStatus SystemFontSelectionGetActive(SystemFontSelectionSnapshot* outSnapshot)
{
	if (!outSnapshot) return SystemFontSelectionStatus::InvalidArgument;
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	for (uint32_t i = 0u; i < sizeof(*outSnapshot); ++i)
		reinterpret_cast<uint8_t*>(outSnapshot)[i] = 0u;
	if (s_activeRecord == kInvalidRecord) builtinIdentity(&outSnapshot->identity);
	else outSnapshot->identity = s_records[s_activeRecord].identity;
	outSnapshot->pixelSize = s_activeSize;
	outSnapshot->selectionGeneration = s_selectionGeneration;
	outSnapshot->isPersisted = s_configured.isPersisted &&
		s_configured.pixelSize == s_activeSize && identityEquals(s_configured.identity, outSnapshot->identity);
	return SystemFontSelectionStatus::Ok;
}

SystemFontSelectionStatus SystemFontSelectionGetConfigured(
	SystemFontConfiguredPreference* outPreference)
{
	if (!outPreference) return SystemFontSelectionStatus::InvalidArgument;
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	*outPreference = s_configured;
	return s_preferenceStatus;
}

SystemFontSelectionStatus SystemFontSelectionGetAvailable(
	SystemFontFaceInfo* output, uint32_t capacity, uint32_t* outCount)
{
	if (!outCount || (!output && capacity != 0u)) return SystemFontSelectionStatus::InvalidArgument;
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	uint32_t count = 0u;
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i)
		if (s_records[i].occupied && s_records[i].available) ++count;
	*outCount = count;
	if (capacity < count) return SystemFontSelectionStatus::OutputTooSmall;
	uint32_t outputIndex = 0u;
	for (uint32_t i = 0u; i < kSystemFontMaxAvailableFonts; ++i) {
		if (!s_records[i].occupied || !s_records[i].available) continue;
		SystemFontFaceInfo& face = output[outputIndex++];
		face = {};
		face.identity = s_records[i].identity;
		face.metadata = s_records[i].metadata;
		face.available = 1u;
		face.active = i == s_activeRecord ? 1u : 0u;
	}
	return SystemFontSelectionStatus::Ok;
}

SystemFontSelectionStatus SystemFontSelectionPrepare(
	const SystemFontIdentity* identity, uint32_t pixelSize,
	SystemFontCandidate* outCandidate)
{
	if (!identity || !outCandidate || identity->kind != SystemFontIdentityKind::TrueType ||
		identity->stableId == 0u) return SystemFontSelectionStatus::InvalidArgument;
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	const int32_t index = findRecordById(identity->stableId);
	if (index < 0 || !identityEquals(*identity, s_records[index].identity) || !s_records[index].available)
		return SystemFontSelectionStatus::NotFound;
	return prepareRecord(static_cast<uint32_t>(index), pixelSize, outCandidate);
}

SystemFontSelectionStatus SystemFontSelectionPrepareActiveSize(
	uint32_t pixelSize, SystemFontCandidate* outCandidate)
{
	if (!outCandidate) return SystemFontSelectionStatus::InvalidArgument;
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	if (s_activeRecord == kInvalidRecord) return SystemFontSelectionStatus::SizeOutOfRange;
	return prepareRecord(s_activeRecord, pixelSize, outCandidate);
}

SystemFontSelectionStatus SystemFontSelectionDrawPreview(
	const SystemFontIdentity* identity, uint32_t pixelSize,
	uint32_t* pixels, size_t destinationBytes, uint32_t pitchBytes,
	uint32_t bufferWidth, uint32_t bufferHeight,
	uint32_t clipX, uint32_t clipY, uint32_t clipWidth, uint32_t clipHeight,
	int32_t x, int32_t topY, const char* utf8, size_t byteLength, uint32_t color)
{
	if (!identity || !pixels || (!utf8 && byteLength != 0u) || byteLength > kFontMaxTextBytes ||
		bufferWidth == 0u || bufferHeight == 0u || bufferWidth > 4096u || bufferHeight > 4096u ||
		pitchBytes < static_cast<uint64_t>(bufferWidth) * sizeof(uint32_t) || (pitchBytes & 3u) != 0u)
		return SystemFontSelectionStatus::InvalidArgument;
	const uint64_t requiredBytes = static_cast<uint64_t>(bufferHeight - 1u) * pitchBytes +
		static_cast<uint64_t>(bufferWidth) * sizeof(uint32_t);
	if (requiredBytes > static_cast<uint64_t>(static_cast<size_t>(-1)))
		return SystemFontSelectionStatus::InvalidArgument;
	if (destinationBytes < static_cast<size_t>(requiredBytes))
		return SystemFontSelectionStatus::InvalidArgument;
	const uint64_t clipRight64 = static_cast<uint64_t>(clipX) + clipWidth;
	const uint64_t clipBottom64 = static_cast<uint64_t>(clipY) + clipHeight;
	const uint32_t clipLeft = clipX < bufferWidth ? clipX : bufferWidth;
	const uint32_t clipTop = clipY < bufferHeight ? clipY : bufferHeight;
	const uint32_t clipRight = static_cast<uint32_t>(clipRight64 < bufferWidth ? clipRight64 : bufferWidth);
	const uint32_t clipBottom = static_cast<uint32_t>(clipBottom64 < bufferHeight ? clipBottom64 : bufferHeight);
	if (clipRight <= clipLeft || clipBottom <= clipTop) return SystemFontSelectionStatus::Ok;

	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	FontHandle handle = s_builtinHandle;
	const bool builtin = identity->kind == SystemFontIdentityKind::BuiltinBitmap;
	if (builtin) {
		SystemFontIdentity expected = {};
		builtinIdentity(&expected);
		if (!identityEquals(*identity, expected) || pixelSize != kBitmapFontPixelHeight)
			return SystemFontSelectionStatus::InvalidArgument;
	} else if (identity->kind == SystemFontIdentityKind::TrueType) {
		const int32_t recordIndex = findRecordById(identity->stableId);
		if (recordIndex < 0 || !s_records[recordIndex].available ||
			!identityEquals(*identity, s_records[recordIndex].identity))
			return SystemFontSelectionStatus::NotFound;
		if (pixelSize < kSystemFontMinSelectablePixelSize || pixelSize > kSystemFontMaxSelectablePixelSize)
			return SystemFontSelectionStatus::SizeOutOfRange;
		const SystemFontSelectionStatus sourceStatus = verifyRecordSource(static_cast<uint32_t>(recordIndex));
		if (sourceStatus != SystemFontSelectionStatus::Ok) return sourceStatus;
		handle = s_records[recordIndex].handle;
	} else {
		return SystemFontSelectionStatus::InvalidArgument;
	}

	TextMeasureResult measured = {};
	const FontStatus measureStatus = FontMeasureText(handle, {pixelSize}, utf8, byteLength, &measured);
	if (measureStatus != FontStatus::Ok && measureStatus != FontStatus::FallbackUsed)
		return fontStatusToSelectionStatus(measureStatus);
	FontMetrics faceMetrics = {};
	if (FontGetMetrics(handle, {pixelSize}, &faceMetrics) != FontStatus::Ok ||
		faceMetrics.lineHeightPx == 0u || faceMetrics.lineHeightPx > 256u)
		return SystemFontSelectionStatus::PreparationFailed;

	int64_t penX = x;
	int64_t lineTop = topY;
	bool previousWasCr = false;
	size_t offset = 0u;
	while (offset < byteLength) {
		uint32_t codePoint = 0u;
		if (!decodePreviewUtf8(utf8, byteLength, &offset, &codePoint))
			return SystemFontSelectionStatus::PreparationFailed;
		if (codePoint == '\r') {
			penX = x;
			lineTop += faceMetrics.lineHeightPx;
			previousWasCr = true;
			continue;
		}
		if (codePoint == '\n') {
			if (!previousWasCr) lineTop += faceMetrics.lineHeightPx;
			penX = x;
			previousWasCr = false;
			continue;
		}
		previousWasCr = false;
		FontGlyphView glyph = {};
		const FontStatus lookupStatus = FontLookupGlyph(handle, {pixelSize}, codePoint, &glyph);
		if (lookupStatus != FontStatus::Ok && lookupStatus != FontStatus::FallbackUsed)
			return fontStatusToSelectionStatus(lookupStatus);
		if (builtin) {
			if (!glyph.bitmap || glyph.bitmapFormat != FontBitmapFormat::ColumnLsbTop ||
				glyph.bitmapByteLength < glyph.metrics.widthPx || glyph.metrics.heightPx > 8u)
				return SystemFontSelectionStatus::PreparationFailed;
			for (uint32_t column = 0u; column < glyph.metrics.widthPx; ++column) {
				const uint8_t bits = glyph.bitmap[column];
				for (uint32_t row = 0u; row < glyph.metrics.heightPx; ++row) {
					if ((bits & (1u << row)) == 0u) continue;
					const int64_t px = penX + glyph.metrics.xBearingPx + column;
					const int64_t py = lineTop + glyph.metrics.yBearingPx + row;
					if (px < clipLeft || px >= clipRight || py < clipTop || py >= clipBottom) continue;
					pixels[static_cast<size_t>(py) * (pitchBytes / 4u) + static_cast<size_t>(px)] = color;
				}
			}
		} else {
			const uint64_t pixelCount = static_cast<uint64_t>(glyph.metrics.widthPx) * glyph.metrics.heightPx;
			if (glyph.metrics.widthPx > kFontTrueTypeMaxBitmapDimension ||
				glyph.metrics.heightPx > kFontTrueTypeMaxBitmapDimension ||
				pixelCount > sizeof(s_previewScratch))
				return SystemFontSelectionStatus::PreparationFailed;
			GlyphMetrics rasterMetrics = glyph.metrics;
			if (pixelCount != 0u) {
				const uint32_t rasterCodePoint = glyph.metrics.codePoint;
				const FontStatus rasterStatus = FontRasterizeGlyph(handle, {pixelSize}, rasterCodePoint,
					s_previewScratch, static_cast<size_t>(pixelCount), glyph.metrics.widthPx,
					&rasterMetrics);
				if (rasterStatus != FontStatus::Ok && rasterStatus != FontStatus::FallbackUsed)
					return fontStatusToSelectionStatus(rasterStatus);
			}
			const int64_t baseline = lineTop + faceMetrics.ascentPx;
			compositePreview(pixels, pitchBytes / 4u, clipLeft, clipTop, clipRight, clipBottom,
				penX + rasterMetrics.xBearingPx, baseline - rasterMetrics.yBearingPx,
				rasterMetrics, s_previewScratch, color);
		}
		penX += glyph.metrics.advancePx;
	}
	return SystemFontSelectionStatus::Ok;
}

SystemFontSelectionStatus SystemFontSelectionApply(const SystemFontCandidate* candidate)
{
	if (!candidate) return SystemFontSelectionStatus::InvalidArgument;
	SystemFontSelectionStatus status = SystemFontSelectionStatus::CandidateStale;
	bool changed = false;
	{
		SelectionGuard guard;
		if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
		if (candidate->token == s_prepared.token && candidate->token != 0u &&
			candidate->identity == s_prepared.identity &&
			candidate->catalogGeneration == s_catalogGeneration &&
			candidate->pixelSize == s_prepared.pixelSize &&
			s_preparedRecord < kSystemFontMaxAvailableFonts &&
			s_records[s_preparedRecord].occupied && s_records[s_preparedRecord].available) {
			const uint32_t priorGeneration = s_selectionGeneration;
			FONT11R_STAGE("selection-apply-core-begin");
			status = applyRecord(s_preparedRecord, candidate->pixelSize, true, true);
			FONT11R_RESULT("selection-apply-core", static_cast<int32_t>(status));
			changed = status == SystemFontSelectionStatus::Ok && s_selectionGeneration != priorGeneration;
		}
		s_prepared = {};
		s_preparedRecord = kInvalidRecord;
	}
	if (changed || status == SystemFontSelectionStatus::PersistenceFailed ||
		status == SystemFontSelectionStatus::RollbackFailed) {
		FONT11R_STAGE("desktop-notification-begin");
		notifyDesktop();
		FONT11R_STAGE("desktop-notification-complete");
	}
	return status;
}

SystemFontSelectionStatus SystemFontSelectionRevert()
{
	SystemFontSelectionStatus status;
	{
		SelectionGuard guard;
		if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
		if (s_previousRecord != kInvalidRecord &&
			(s_previousRecord >= kSystemFontMaxAvailableFonts ||
			!s_records[s_previousRecord].occupied || !s_records[s_previousRecord].available))
			return SystemFontSelectionStatus::NoPreviousSelection;
		const uint32_t target = s_previousRecord;
		const uint32_t size = s_previousSize;
		status = applyRecord(target, size, true, target != kInvalidRecord);
	}
	if (status == SystemFontSelectionStatus::Ok || status == SystemFontSelectionStatus::PersistenceFailed)
		notifyDesktop();
	return status;
}

SystemFontSelectionStatus SystemFontSelectionResetToBuiltin()
{
	SystemFontSelectionStatus status;
	{
		SelectionGuard guard;
		if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
		status = applyRecord(kInvalidRecord, kBitmapFontPixelHeight, true, false);
	}
	if (status == SystemFontSelectionStatus::Ok || status == SystemFontSelectionStatus::PersistenceFailed)
		notifyDesktop();
	return status;
}

SystemFontSelectionStatus SystemFontSelectionPreferenceStatus()
{
	SelectionGuard guard;
	if (!s_initialized) return SystemFontSelectionStatus::NotInitialized;
	return s_preferenceStatus;
}

const char* SystemFontSelectionStatusName(SystemFontSelectionStatus status)
{
	switch (status) {
	case SystemFontSelectionStatus::Ok: return "Ok";
	case SystemFontSelectionStatus::NotInitialized: return "NotInitialized";
	case SystemFontSelectionStatus::InvalidArgument: return "InvalidArgument";
	case SystemFontSelectionStatus::DirectoryUnavailable: return "DirectoryUnavailable";
	case SystemFontSelectionStatus::DiscoveryLimitReached: return "DiscoveryLimitReached";
	case SystemFontSelectionStatus::RegistryCapacity: return "RegistryCapacity";
	case SystemFontSelectionStatus::SourceCapacity: return "SourceCapacity";
	case SystemFontSelectionStatus::InvalidFont: return "InvalidFont";
	case SystemFontSelectionStatus::UnsupportedFont: return "UnsupportedFont";
	case SystemFontSelectionStatus::NotFound: return "NotFound";
	case SystemFontSelectionStatus::StaleIdentity: return "StaleIdentity";
	case SystemFontSelectionStatus::SizeOutOfRange: return "SizeOutOfRange";
	case SystemFontSelectionStatus::PreparationFailed: return "PreparationFailed";
	case SystemFontSelectionStatus::CandidateStale: return "CandidateStale";
	case SystemFontSelectionStatus::PreferenceCorrupt: return "PreferenceCorrupt";
	case SystemFontSelectionStatus::PreferenceUnsupported: return "PreferenceUnsupported";
	case SystemFontSelectionStatus::PersistenceFailed: return "PersistenceFailed";
	case SystemFontSelectionStatus::GenerationExhausted: return "GenerationExhausted";
	case SystemFontSelectionStatus::NoPreviousSelection: return "NoPreviousSelection";
	case SystemFontSelectionStatus::RollbackFailed: return "RollbackFailed";
	case SystemFontSelectionStatus::OutputTooSmall: return "OutputTooSmall";
	default: return "Unknown";
	}
}

namespace internal {

bool systemFontSelectionGetRenderState(FontHandle* outHandle,
	FontSize* outSize, uint32_t* outSelectionGeneration)
{
	if (!outHandle || !outSize) return false;
	SelectionGuard guard;
	if (!s_initialized) return false;
	if (outSelectionGeneration) *outSelectionGeneration = s_selectionGeneration;
	if (s_activeRecord == kInvalidRecord ||
		s_activeRecord >= kSystemFontMaxAvailableFonts || !s_records[s_activeRecord].occupied)
		return false;
	*outHandle = s_records[s_activeRecord].handle;
	outSize->pixelHeight = s_activeSize;
	return true;
}

} // namespace internal
} // namespace gui
} // namespace gxos
