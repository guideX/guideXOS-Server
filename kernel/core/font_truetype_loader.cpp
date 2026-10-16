#include "include/kernel/font_contract.h"
#include "include/kernel/font_truetype.h"
#include "include/kernel/vfs.h"

namespace gxos {
namespace gui {
namespace {

struct FontSourceRecord {
	uint8_t state; // 0=free, 1=loading, 2=registered
	uint8_t reserved[3];
	uint32_t sourceLength;
	FontId id;
	FontTrueTypeMetadata metadata;
	FontDefinition definition;
	uint8_t source[kFontTrueTypeMaxFileBytes];
};

FontSourceRecord s_sourceRecords[kFontTrueTypeMaxExternalFonts] = {};
volatile uint32_t s_sourceLock = 0;

const FontBackend kMetadataOnlyBackend = {
	FontCapabilityMetadataOnly,
	nullptr,
	nullptr,
};

void lockSources()
{
	while (__sync_lock_test_and_set(&s_sourceLock, 1u)) {
		while (s_sourceLock != 0) { }
	}
}

void unlockSources()
{
	__sync_lock_release(&s_sourceLock);
}

struct SourceGuard {
	SourceGuard() { lockSources(); }
	~SourceGuard() { unlockSources(); }
};

void clearRecord(FontSourceRecord* record)
{
	if (!record) return;
	const uint32_t length = record->sourceLength;
	for (uint32_t i = 0; i < length && i < kFontTrueTypeMaxFileBytes; ++i)
		record->source[i] = 0;
	record->state = 0;
	record->sourceLength = 0;
	record->id.value = 0;
	for (size_t i = 0; i < sizeof(record->metadata); ++i)
		reinterpret_cast<uint8_t*>(&record->metadata)[i] = 0;
	record->definition.id.value = 0;
	record->definition.debugName = nullptr;
	record->definition.backend = nullptr;
	record->definition.context = nullptr;
	record->definition.trueTypeMetadata = nullptr;
	record->definition.onUnregister = nullptr;
}

FontSourceRecord* reserveRecord(FontId id)
{
	SourceGuard guard;
	for (uint32_t i = 0; i < kFontTrueTypeMaxExternalFonts; ++i) {
		if (s_sourceRecords[i].state != 0u) continue;
		s_sourceRecords[i].state = 1u;
		s_sourceRecords[i].id = id;
		s_sourceRecords[i].sourceLength = 0;
		return &s_sourceRecords[i];
	}
	return nullptr;
}

void releaseSourceContext(const void* context)
{
	SourceGuard guard;
	for (uint32_t i = 0; i < kFontTrueTypeMaxExternalFonts; ++i) {
		if (context == &s_sourceRecords[i]) {
			clearRecord(&s_sourceRecords[i]);
			return;
		}
	}
}

void abandonRecord(FontSourceRecord* record)
{
	SourceGuard guard;
	clearRecord(record);
}

FontStatus parserStatusToFontStatus(FontTrueTypeStatus status)
{
	if (status == FontTrueTypeOk) return FontStatus::Ok;
	if (status == FontTrueTypeFileTooLarge) return FontStatus::FontFileTooLarge;
	if (status == FontTrueTypeUnsupportedSignature ||
		status == FontTrueTypeUnsupportedCollection ||
		status == FontTrueTypeUnsupportedOutline ||
		status == FontTrueTypeUnsupportedVariant ||
		status == FontTrueTypeUnsupportedUnicodeMap)
		return FontStatus::UnsupportedFontFormat;
	return FontStatus::InvalidFontFile;
}

FontStatus finishLoad(FontSourceRecord* record, FontHandle* outHandle,
	FontTrueTypeMetadata* outMetadata)
{
	const FontTrueTypeStatus parseStatus = FontTrueTypeValidate(record->source,
		record->sourceLength, &record->metadata);
	if (parseStatus != FontTrueTypeOk) {
		const FontStatus status = parserStatusToFontStatus(parseStatus);
		abandonRecord(record);
		return status;
	}
	FontDefinition& definition = record->definition;
	definition.id = record->id;
	definition.debugName = record->metadata.familyName;
	definition.backend = &kMetadataOnlyBackend;
	definition.context = record;
	definition.trueTypeMetadata = &record->metadata;
	definition.onUnregister = &releaseSourceContext;
	FontHandle handle = {0xffffu, 0, 0};
	const FontStatus registerStatus = FontManagerRegisterFont(&definition, &handle);
	if (registerStatus != FontStatus::Ok) {
		abandonRecord(record);
		return registerStatus;
	}
	FontTrueTypeMetadata metadataCopy = {};
	{
		SourceGuard guard;
		if (record->state != 1u) {
			if (outHandle) *outHandle = {0xffffu, 0, 0};
			return FontStatus::InvalidHandle;
		}
		record->state = 2u;
		metadataCopy = record->metadata;
	}
	if (outHandle) *outHandle = handle;
	if (outMetadata) *outMetadata = metadataCopy;
	return FontStatus::Ok;
}

bool pathLengthIsValid(const char* path)
{
	if (!path) return false;
	for (size_t i = 0; i < kernel::vfs::VFS_MAX_PATH; ++i)
		if (path[i] == '\0') return i != 0;
	return false;
}

} // namespace

FontStatus FontTrueTypeLoadMemory(FontId id, const uint8_t* inputBytes,
	size_t inputLength, FontHandle* outHandle,
	FontTrueTypeMetadata* outMetadata)
{
	if (outHandle) *outHandle = {0xffffu, 0, 0};
	if (outMetadata) {
		for (size_t i = 0; i < sizeof(*outMetadata); ++i)
			reinterpret_cast<uint8_t*>(outMetadata)[i] = 0;
	}
	if (!outHandle || !inputBytes || id.value == 0 || inputLength == 0)
		return FontStatus::InvalidArgument;
	if (inputLength > kFontTrueTypeMaxFileBytes) return FontStatus::FontFileTooLarge;
	if (!FontManagerIsInitialized()) return FontStatus::NotInitialized;
	FontSourceRecord* record = reserveRecord(id);
	if (!record) return FontStatus::FontSourceCapacity;
	record->sourceLength = static_cast<uint32_t>(inputLength);
	for (uint32_t i = 0; i < record->sourceLength; ++i) record->source[i] = inputBytes[i];
	return finishLoad(record, outHandle, outMetadata);
}

FontStatus FontTrueTypeLoadFile(const char* path, FontId id,
	FontHandle* outHandle, FontTrueTypeMetadata* outMetadata)
{
	if (outHandle) *outHandle = {0xffffu, 0, 0};
	if (outMetadata) {
		for (size_t i = 0; i < sizeof(*outMetadata); ++i)
			reinterpret_cast<uint8_t*>(outMetadata)[i] = 0;
	}
	if (!outHandle || !pathLengthIsValid(path) || id.value == 0)
		return FontStatus::InvalidArgument;
	if (!FontManagerIsInitialized()) return FontStatus::NotInitialized;
	kernel::vfs::FileInfo info = {};
	const kernel::vfs::Status statStatus = kernel::vfs::stat(path, &info);
	if (statStatus == kernel::vfs::VFS_ERR_NOT_FOUND) return FontStatus::FontFileNotFound;
	if (statStatus != kernel::vfs::VFS_OK) return FontStatus::FontFileIoError;
	if (info.type != kernel::vfs::FILE_TYPE_REGULAR) return FontStatus::FontFileIoError;
	if (info.size == 0u) return FontStatus::InvalidFontFile;
	if (info.size > kFontTrueTypeMaxFileBytes) return FontStatus::FontFileTooLarge;
	FontSourceRecord* record = reserveRecord(id);
	if (!record) return FontStatus::FontSourceCapacity;
	record->sourceLength = static_cast<uint32_t>(info.size);
	const int32_t bytesRead = kernel::vfs::read_file(path, record->source,
		record->sourceLength);
	if (bytesRead < 0 || static_cast<uint32_t>(bytesRead) != record->sourceLength) {
		abandonRecord(record);
		return FontStatus::FontFileIoError;
	}
	kernel::vfs::FileInfo afterRead = {};
	const kernel::vfs::Status afterStatus = kernel::vfs::stat(path, &afterRead);
	if (afterStatus != kernel::vfs::VFS_OK || afterRead.type != kernel::vfs::FILE_TYPE_REGULAR ||
		afterRead.size != info.size) {
		abandonRecord(record);
		return FontStatus::FontFileIoError;
	}
	return finishLoad(record, outHandle, outMetadata);
}

} // namespace gui
} // namespace gxos
