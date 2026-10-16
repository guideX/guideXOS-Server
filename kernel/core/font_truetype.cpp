#include "include/kernel/font_truetype.h"

namespace gxos {
namespace gui {
namespace {

struct TableRecord {
	uint32_t tag;
	uint32_t offset;
	uint32_t length;
};

static const uint32_t kSfntTrueType = 0x00010000u;
static const uint32_t kSfntCollection = 0x74746366u; // ttcf
static const uint32_t kSfntCff = 0x4f54544fu; // OTTO
static const uint32_t kHeadTag = 0x68656164u;
static const uint32_t kMaxpTag = 0x6d617870u;
static const uint32_t kCmapTag = 0x636d6170u;
static const uint32_t kHheaTag = 0x68686561u;
static const uint32_t kHmtxTag = 0x686d7478u;
static const uint32_t kLocaTag = 0x6c6f6361u;
static const uint32_t kGlyfTag = 0x676c7966u;
static const uint32_t kNameTag = 0x6e616d65u;
static const uint32_t kCffTag = 0x43464620u;
static const uint32_t kCff2Tag = 0x43464632u;
static const uint32_t kFvarTag = 0x66766172u;
static const uint32_t kGvarTag = 0x67766172u;
static const uint32_t kAvarTag = 0x61766172u;
static const uint32_t kCvarTag = 0x63766172u;
static const uint32_t kHvarTag = 0x48564152u;
static const uint32_t kVvarTag = 0x56564152u;
static const uint32_t kMvarTag = 0x4d564152u;

bool rangeFits(size_t total, uint32_t offset, uint32_t length)
{
	return static_cast<size_t>(offset) <= total &&
		static_cast<size_t>(length) <= total - static_cast<size_t>(offset);
}

bool addSize(size_t left, size_t right, size_t* out)
{
	if (!out || right > static_cast<size_t>(-1) - left) return false;
	*out = left + right;
	return true;
}

bool multiplySize(size_t left, size_t right, size_t* out)
{
	if (!out || (left != 0 && right > static_cast<size_t>(-1) / left)) return false;
	*out = left * right;
	return true;
}

uint16_t readU16(const uint8_t* bytes, size_t offset)
{
	return static_cast<uint16_t>((static_cast<uint16_t>(bytes[offset]) << 8) |
		static_cast<uint16_t>(bytes[offset + 1u]));
}

uint32_t readU32(const uint8_t* bytes, size_t offset)
{
	return (static_cast<uint32_t>(bytes[offset]) << 24) |
		(static_cast<uint32_t>(bytes[offset + 1u]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 2u]) << 8) |
		static_cast<uint32_t>(bytes[offset + 3u]);
}

int16_t readS16(const uint8_t* bytes, size_t offset)
{
	const uint16_t raw = readU16(bytes, offset);
	const int32_t signedValue = (raw & 0x8000u) ?
		static_cast<int32_t>(raw) - 65536 : static_cast<int32_t>(raw);
	return static_cast<int16_t>(signedValue);
}

const TableRecord* findTable(const TableRecord* tables, uint16_t count, uint32_t tag)
{
	for (uint16_t i = 0; i < count; ++i)
		if (tables[i].tag == tag) return &tables[i];
	return nullptr;
}

bool appendUtf8(uint32_t cp, char* output, size_t capacity, size_t* outLength)
{
	if (!output || !outLength || cp == 0 || cp < 0x20u || cp > 0x10ffffu ||
		(cp >= 0xd800u && cp <= 0xdfffu)) return false;
	uint8_t encoded[4];
	size_t count = 0;
	if (cp <= 0x7fu) {
		encoded[0] = static_cast<uint8_t>(cp);
		count = 1;
	} else if (cp <= 0x7ffu) {
		encoded[0] = static_cast<uint8_t>(0xc0u | (cp >> 6));
		encoded[1] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
		count = 2;
	} else if (cp <= 0xffffu) {
		encoded[0] = static_cast<uint8_t>(0xe0u | (cp >> 12));
		encoded[1] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3fu));
		encoded[2] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
		count = 3;
	} else {
		encoded[0] = static_cast<uint8_t>(0xf0u | (cp >> 18));
		encoded[1] = static_cast<uint8_t>(0x80u | ((cp >> 12) & 0x3fu));
		encoded[2] = static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3fu));
		encoded[3] = static_cast<uint8_t>(0x80u | (cp & 0x3fu));
		count = 4;
	}
	if (*outLength > capacity || count > capacity - *outLength) return false;
	for (size_t i = 0; i < count; ++i) output[*outLength + i] = static_cast<char>(encoded[i]);
	*outLength += count;
	return true;
}

bool decodeUtf16BeName(const uint8_t* data, uint16_t byteLength,
	char output[kFontTrueTypeMaxNameBytes + 1u])
{
	if ((byteLength & 1u) != 0 || byteLength > kFontTrueTypeMaxMetadataStringBytes)
		return false;
	size_t outputLength = 0;
	for (uint32_t offset = 0; offset < byteLength; offset += 2u) {
		uint32_t cp = readU16(data, offset);
		if (cp >= 0xd800u && cp <= 0xdbffu) {
			if (offset + 3u >= byteLength) return false;
			const uint32_t low = readU16(data, offset + 2u);
			if (low < 0xdc00u || low > 0xdfffu) return false;
			cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
			offset += 2u;
		} else if (cp >= 0xdc00u && cp <= 0xdfffu) {
			return false;
		}
		if (!appendUtf8(cp, output, kFontTrueTypeMaxNameBytes, &outputLength)) return false;
	}
	if (outputLength == 0) return false;
	output[outputLength] = '\0';
	return true;
}

bool validateNameTable(const uint8_t* bytes, const TableRecord& name,
	FontTrueTypeMetadata* metadata)
{
	if (name.length < 6u) return false;
	const uint8_t* table = bytes + name.offset;
	const uint16_t format = readU16(table, 0);
	const uint16_t count = readU16(table, 2);
	const uint16_t storageOffset = readU16(table, 4);
	if ((format != 0u && format != 1u) || count > kFontTrueTypeMaxNameRecords)
		return false;
	size_t recordsBytes = 0;
	size_t recordsEnd = 0;
	if (!multiplySize(count, 12u, &recordsBytes) ||
		!addSize(6u, recordsBytes, &recordsEnd) || recordsEnd > name.length)
		return false;
	if (storageOffset < recordsEnd || storageOffset > name.length) return false;
	if (format == 1u) {
		if (name.length - recordsEnd < 2u) return false;
		const uint16_t languageTagCount = readU16(table, recordsEnd);
		if (languageTagCount > kFontTrueTypeMaxNameRecords) return false;
		size_t languageBytes = 0;
		size_t languageEnd = 0;
		if (!multiplySize(languageTagCount, 4u, &languageBytes) ||
			!addSize(recordsEnd + 2u, languageBytes, &languageEnd) ||
			languageEnd > storageOffset) return false;
		for (uint16_t i = 0; i < languageTagCount; ++i) {
			const size_t record = recordsEnd + 2u + static_cast<size_t>(i) * 4u;
			const uint16_t length = readU16(table, record);
			const uint16_t offset = readU16(table, record + 2u);
			if (static_cast<size_t>(offset) > name.length - storageOffset ||
				static_cast<size_t>(length) > name.length - storageOffset - offset)
				return false;
		}
	}

	int familyRank = -1;
	int styleRank = -1;
	for (uint16_t i = 0; i < count; ++i) {
		const size_t record = 6u + static_cast<size_t>(i) * 12u;
		const uint16_t platform = readU16(table, record);
		const uint16_t encoding = readU16(table, record + 2u);
		const uint16_t language = readU16(table, record + 4u);
		const uint16_t nameId = readU16(table, record + 6u);
		const uint16_t length = readU16(table, record + 8u);
		const uint16_t offset = readU16(table, record + 10u);
		if (static_cast<size_t>(offset) > name.length - storageOffset ||
			static_cast<size_t>(length) > name.length - storageOffset - offset)
			return false;
		const bool unicodeText = platform == 0u ||
			(platform == 3u && (encoding == 1u || encoding == 10u));
		if (!unicodeText || (nameId != 1u && nameId != 2u && nameId != 16u && nameId != 17u))
			continue;
		char decoded[kFontTrueTypeMaxNameBytes + 1u] = {};
		if (!decodeUtf16BeName(table + storageOffset + offset, length, decoded)) continue;
		const bool family = nameId == 1u || nameId == 16u;
		const bool preferred = nameId == 16u || nameId == 17u;
		int rank = preferred ? 100 : 0;
		if (language == 0x0409u) rank += 20;
		if (platform == 3u) rank += 5;
		int& bestRank = family ? familyRank : styleRank;
		if (rank > bestRank) {
			char* destination = family ? metadata->familyName : metadata->styleName;
			for (size_t j = 0; j < kFontTrueTypeMaxNameBytes + 1u; ++j)
				destination[j] = decoded[j];
			bestRank = rank;
		}
	}
	if (metadata->familyName[0] == '\0') {
		const char fallback[] = "Unknown";
		for (size_t i = 0; i < sizeof(fallback); ++i) metadata->familyName[i] = fallback[i];
	}
	if (metadata->styleName[0] == '\0') {
		const char fallback[] = "Regular";
		for (size_t i = 0; i < sizeof(fallback); ++i) metadata->styleName[i] = fallback[i];
	}
	return true;
}

bool validateCmapFormat4(const uint8_t* subtable, uint32_t availableLength,
	uint32_t* outLength)
{
	if (availableLength < 16u) return false;
	const uint16_t length = readU16(subtable, 2u);
	const uint16_t segCountX2 = readU16(subtable, 6u);
	if (length < 16u || length > availableLength || segCountX2 == 0u || (segCountX2 & 1u))
		return false;
	const uint32_t segmentCount = segCountX2 / 2u;
	if (segmentCount > 8192u) return false;
	const uint32_t minimumLength = 16u + segmentCount * 8u;
	if (length < minimumLength) return false;
	const size_t endCodes = 14u;
	const size_t starts = endCodes + segmentCount * 2u + 2u;
	uint16_t previousEnd = 0;
	for (uint32_t i = 0; i < segmentCount; ++i) {
		const uint16_t endCode = readU16(subtable, endCodes + i * 2u);
		const uint16_t startCode = readU16(subtable, starts + i * 2u);
		if (startCode > endCode || (i != 0 && endCode <= previousEnd)) return false;
		previousEnd = endCode;
	}
	*outLength = length;
	return true;
}

bool validateCmapFormat12(const uint8_t* subtable, uint32_t availableLength,
	uint16_t glyphCount, uint32_t* outLength)
{
	if (availableLength < 16u || readU16(subtable, 2u) != 0u) return false;
	const uint32_t length = readU32(subtable, 4u);
	const uint32_t groupCount = readU32(subtable, 12u);
	if (length < 16u || length > availableLength || groupCount == 0u ||
		groupCount > kFontTrueTypeMaxCmapGroups)
		return false;
	size_t groupBytes = 0;
	size_t required = 0;
	if (!multiplySize(groupCount, 12u, &groupBytes) ||
		!addSize(16u, groupBytes, &required) || required > length) return false;
	uint32_t previousEnd = 0;
	for (uint32_t i = 0; i < groupCount; ++i) {
		const size_t group = 16u + static_cast<size_t>(i) * 12u;
		const uint32_t start = readU32(subtable, group);
		const uint32_t end = readU32(subtable, group + 4u);
		const uint32_t firstGlyph = readU32(subtable, group + 8u);
		if (start > end || end > 0x10ffffu ||
			(start <= 0xdfffu && end >= 0xd800u) ||
			(i != 0 && start <= previousEnd) || firstGlyph >= glyphCount ||
			end - start > static_cast<uint32_t>(glyphCount - 1u) - firstGlyph)
			return false;
		previousEnd = end;
	}
	*outLength = length;
	return true;
}

FontTrueTypeStatus validateCmap(const uint8_t* bytes, const TableRecord& cmap,
	uint16_t glyphCount, uint8_t* outFlags)
{
	if (!outFlags || cmap.length < 4u) return FontTrueTypeInvalidRequiredTable;
	const uint8_t* table = bytes + cmap.offset;
	if (readU16(table, 0u) != 0u) return FontTrueTypeInvalidRequiredTable;
	const uint16_t count = readU16(table, 2u);
	if (count == 0u || count > kFontTrueTypeMaxCmapRecords ||
		static_cast<uint32_t>(count) > (cmap.length - 4u) / 8u)
		return FontTrueTypeInvalidRequiredTable;
	uint8_t flags = 0;
	for (uint16_t i = 0; i < count; ++i) {
		const size_t record = 4u + static_cast<size_t>(i) * 8u;
		const uint16_t platform = readU16(table, record);
		const uint16_t encoding = readU16(table, record + 2u);
		const uint32_t offset = readU32(table, record + 4u);
		const uint32_t recordsEnd = 4u + static_cast<uint32_t>(count) * 8u;
		if (offset < recordsEnd || offset > cmap.length || cmap.length - offset < 2u)
			return FontTrueTypeInvalidRequiredTable;
		const uint8_t* subtable = table + offset;
		const uint16_t format = readU16(subtable, 0u);
		const bool unicode = platform == 0u ||
			(platform == 3u && (encoding == 1u || encoding == 10u));
		if (!unicode) continue;
		uint32_t subtableLength = 0;
		bool valid = false;
		if (format == 4u)
			valid = validateCmapFormat4(subtable, cmap.length - offset, &subtableLength);
		else if (format == 12u)
			valid = validateCmapFormat12(subtable, cmap.length - offset, glyphCount, &subtableLength);
		if ((format == 4u || format == 12u) && !valid)
			return FontTrueTypeInvalidRequiredTable;
		if (!valid) continue;
		if (format == 4u) flags |= FontTrueTypeCmapFormat4;
		if (format == 12u) flags |= FontTrueTypeCmapFormat12;
		if (platform == 0u) flags |= FontTrueTypeCmapUnicodePlatform;
		if (platform == 3u && encoding == 1u) flags |= FontTrueTypeCmapWindowsBmp;
		if (platform == 3u && encoding == 10u) flags |= FontTrueTypeCmapWindowsFull;
	}
	if ((flags & (FontTrueTypeCmapFormat4 | FontTrueTypeCmapFormat12)) == 0u)
		return FontTrueTypeUnsupportedUnicodeMap;
	*outFlags = flags;
	return FontTrueTypeOk;
}

} // namespace

FontTrueTypeStatus FontTrueTypeValidate(const uint8_t* inputBytes,
	size_t inputLength, FontTrueTypeMetadata* outMetadata)
{
	if (outMetadata) {
		for (size_t i = 0; i < sizeof(*outMetadata); ++i)
			reinterpret_cast<uint8_t*>(outMetadata)[i] = 0;
	}
	if (!inputBytes || !outMetadata || inputLength == 0)
		return FontTrueTypeInvalidArgument;
	if (inputLength > kFontTrueTypeMaxFileBytes) return FontTrueTypeFileTooLarge;
	if (inputLength < 12u) return FontTrueTypeInvalidOffsetTable;

	const uint32_t signature = readU32(inputBytes, 0u);
	if (signature == kSfntCollection) return FontTrueTypeUnsupportedCollection;
	if (signature == kSfntCff) return FontTrueTypeUnsupportedOutline;
	if (signature != kSfntTrueType) return FontTrueTypeUnsupportedSignature;
	const uint16_t tableCount = readU16(inputBytes, 4u);
	if (tableCount == 0u) return FontTrueTypeInvalidOffsetTable;
	if (tableCount > kFontTrueTypeMaxTables) return FontTrueTypeTooManyTables;
	uint16_t powerOfTwo = 1u;
	uint16_t selector = 0u;
	while (static_cast<uint32_t>(powerOfTwo) * 2u <= tableCount) {
		powerOfTwo = static_cast<uint16_t>(powerOfTwo * 2u);
		++selector;
	}
	const uint16_t searchRange = static_cast<uint16_t>(powerOfTwo * 16u);
	const uint16_t rangeShift = static_cast<uint16_t>(tableCount * 16u - searchRange);
	if (readU16(inputBytes, 6u) != searchRange || readU16(inputBytes, 8u) != selector ||
		readU16(inputBytes, 10u) != rangeShift)
		return FontTrueTypeInvalidOffsetTable;
	size_t directoryBytes = 0;
	size_t directoryEnd = 0;
	if (!multiplySize(tableCount, 16u, &directoryBytes) ||
		!addSize(12u, directoryBytes, &directoryEnd) || directoryEnd > inputLength)
		return FontTrueTypeInvalidOffsetTable;

	TableRecord tables[kFontTrueTypeMaxTables] = {};
	for (uint16_t i = 0; i < tableCount; ++i) {
		const size_t entry = 12u + static_cast<size_t>(i) * 16u;
		tables[i].tag = readU32(inputBytes, entry);
		tables[i].offset = readU32(inputBytes, entry + 8u);
		tables[i].length = readU32(inputBytes, entry + 12u);
		if ((tables[i].offset & 3u) != 0u ||
			!rangeFits(inputLength, tables[i].offset, tables[i].length) ||
			(tables[i].length != 0u && tables[i].offset < directoryEnd))
			return FontTrueTypeInvalidTableRange;
		for (uint16_t j = 0; j < i; ++j) {
			if (tables[i].tag == tables[j].tag) return FontTrueTypeDuplicateTable;
			if (tables[i].length != 0u && tables[j].length != 0u) {
				const uint64_t endI = static_cast<uint64_t>(tables[i].offset) + tables[i].length;
				const uint64_t endJ = static_cast<uint64_t>(tables[j].offset) + tables[j].length;
				if (tables[i].offset < endJ && tables[j].offset < endI)
					return FontTrueTypeInvalidTableRange;
			}
		}
	}
	if (findTable(tables, tableCount, kCffTag) || findTable(tables, tableCount, kCff2Tag))
		return FontTrueTypeUnsupportedOutline;
	if (findTable(tables, tableCount, kFvarTag) || findTable(tables, tableCount, kGvarTag) ||
		findTable(tables, tableCount, kAvarTag) || findTable(tables, tableCount, kCvarTag) ||
		findTable(tables, tableCount, kHvarTag) || findTable(tables, tableCount, kVvarTag) ||
		findTable(tables, tableCount, kMvarTag))
		return FontTrueTypeUnsupportedVariant;

	const TableRecord* head = findTable(tables, tableCount, kHeadTag);
	const TableRecord* maxp = findTable(tables, tableCount, kMaxpTag);
	const TableRecord* cmap = findTable(tables, tableCount, kCmapTag);
	const TableRecord* hhea = findTable(tables, tableCount, kHheaTag);
	const TableRecord* hmtx = findTable(tables, tableCount, kHmtxTag);
	const TableRecord* loca = findTable(tables, tableCount, kLocaTag);
	const TableRecord* glyf = findTable(tables, tableCount, kGlyfTag);
	const TableRecord* name = findTable(tables, tableCount, kNameTag);
	if (!head || !maxp || !cmap || !hhea || !hmtx || !loca || !glyf || !name)
		return FontTrueTypeMissingRequiredTable;
	if (head->length < 54u || maxp->length < 32u || hhea->length < 36u ||
		hmtx->length == 0u || loca->length == 0u || glyf->length == 0u)
		return FontTrueTypeInvalidRequiredTable;

	const uint8_t* headBytes = inputBytes + head->offset;
	const uint8_t* maxpBytes = inputBytes + maxp->offset;
	const uint8_t* hheaBytes = inputBytes + hhea->offset;
	if (readU32(headBytes, 0u) != 0x00010000u ||
		readU32(headBytes, 12u) != 0x5f0f3cf5u ||
		readU32(maxpBytes, 0u) != 0x00010000u ||
		readU32(hheaBytes, 0u) != 0x00010000u)
		return FontTrueTypeInvalidRequiredTable;
	const uint16_t unitsPerEm = readU16(headBytes, 18u);
	const int16_t indexToLocFormat = readS16(headBytes, 50u);
	const uint16_t glyphCount = readU16(maxpBytes, 4u);
	const uint16_t numberOfHMetrics = readU16(hheaBytes, 34u);
	if (unitsPerEm < 16u || unitsPerEm > 16384u || glyphCount == 0u ||
		glyphCount > kFontTrueTypeMaxGlyphs || numberOfHMetrics == 0u ||
		numberOfHMetrics > glyphCount || (indexToLocFormat != 0 && indexToLocFormat != 1))
		return FontTrueTypeInvalidRequiredTable;

	size_t metricsBytes = 0;
	size_t trailingMetricsBytes = 0;
	size_t requiredHmtx = 0;
	if (!multiplySize(numberOfHMetrics, 4u, &metricsBytes) ||
		!multiplySize(glyphCount - numberOfHMetrics, 2u, &trailingMetricsBytes) ||
		!addSize(metricsBytes, trailingMetricsBytes, &requiredHmtx) ||
		requiredHmtx > hmtx->length)
		return FontTrueTypeInvalidRequiredTable;
	const size_t locaEntryBytes = indexToLocFormat == 0 ? 2u : 4u;
	size_t locaRequired = 0;
	if (!multiplySize(static_cast<size_t>(glyphCount) + 1u, locaEntryBytes, &locaRequired) ||
		locaRequired > loca->length)
		return FontTrueTypeInvalidRequiredTable;
	const uint8_t* locaBytes = inputBytes + loca->offset;
	uint32_t previousLocation = 0;
	for (uint32_t i = 0; i <= glyphCount; ++i) {
		const uint32_t location = indexToLocFormat == 0 ?
			static_cast<uint32_t>(readU16(locaBytes, static_cast<size_t>(i) * 2u)) * 2u :
			readU32(locaBytes, static_cast<size_t>(i) * 4u);
		if (location < previousLocation || location > glyf->length)
			return FontTrueTypeInvalidRequiredTable;
		previousLocation = location;
	}

	FontTrueTypeMetadata candidate = {};
	candidate.fileBytes = static_cast<uint32_t>(inputLength);
	candidate.tableCount = tableCount;
	candidate.unitsPerEm = unitsPerEm;
	candidate.glyphCount = glyphCount;
	candidate.numberOfHorizontalMetrics = numberOfHMetrics;
	candidate.advanceWidthMax = readU16(hheaBytes, 10u);
	candidate.ascender = readS16(hheaBytes, 4u);
	candidate.descender = readS16(hheaBytes, 6u);
	candidate.lineGap = readS16(hheaBytes, 8u);
	candidate.faceCount = 1u;
	if (!validateNameTable(inputBytes, *name, &candidate))
		return FontTrueTypeInvalidNameTable;
	const FontTrueTypeStatus cmapStatus = validateCmap(inputBytes, *cmap,
		glyphCount, &candidate.cmapFlags);
	if (cmapStatus != FontTrueTypeOk) return cmapStatus;
	*outMetadata = candidate;
	return FontTrueTypeOk;
}

} // namespace gui
} // namespace gxos
