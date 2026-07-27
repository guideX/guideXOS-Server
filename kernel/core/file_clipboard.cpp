//
// guideXOS bare-metal file clipboard implementation
//

#include "include/kernel/file_clipboard.h"
#include "include/kernel/build_identity.h"
#include "include/kernel/fs_fat.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace file_clipboard {

static const uint32_t kCopyBufferBytes = 64u * 1024u;
static const uint64_t kMaxFatFileBytes = 0xFFFFFFFFull;
static const int kMaxNameCandidates = 1000;
static const uint32_t kMaxTreeDepth = 64;
static const size_t kMaxDirectoryEntriesPerOperation = 4096;
static const char* kTrashRootPath = "/Trash";
static const char* kTrashMetadataExtension = ".INF";
static const char* kTrashMetadataPrefix = "TR";

// File operations are deliberately bounded. The fixed buffer is in BSS and
// is shared by the synchronous paste path, so paste never allocates a
// file-sized stack or heap buffer.
static uint8_t s_copyBuffer[kCopyBufferBytes];
static char s_sourcePath[vfs::VFS_MAX_PATH] = {0};
static char s_sourceName[vfs::VFS_MAX_FILENAME] = {0};
static Operation s_operation = Operation::None;
static uint64_t s_operationGeneration = 0;
static PasteDiagnostic s_lastDiagnostic{};
static char s_diagnosticMessage[192] = {0};
static uint64_t s_progressCounter = 0;
static uint64_t s_sourceSize = 0;
static bool s_sourceIsRegularFile = false;
static bool s_operationActive = false;
static ProgressCallback s_progressCallback = nullptr;
static FileOperationProgress s_progress{};

static size_t local_strlen(const char* value) {
    if (!value) return 0;
    size_t length = 0;
    while (value[length]) ++length;
    return length;
}

static bool copy_text(char* destination, size_t destinationSize, const char* source) {
    if (!destination || destinationSize == 0 || !source) return false;
    size_t length = local_strlen(source);
    if (length >= destinationSize) {
        destination[0] = '\0';
        return false;
    }
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return true;
}

static void notify_progress() {
    if (s_progressCallback) s_progressCallback(s_progress);
}

static void set_operation_state(OperationState state) {
    s_progress.state = state;
    notify_progress();
}

static void begin_operation_progress(const char* destinationDirectory) {
    s_progress = FileOperationProgress{};
    s_progress.operation = s_operation;
    s_progress.state = OperationState::Preparing;
    s_progress.phase = PasteStage::SourceValidation;
    copy_text(s_progress.sourceDisplayName, sizeof(s_progress.sourceDisplayName), s_sourceName);
    s_progress.totalKnown = s_sourceIsRegularFile;
    s_progress.totalBytes = s_sourceIsRegularFile ? s_sourceSize : 0;
    if (destinationDirectory) {
        vfs::normalize_path(destinationDirectory, s_progress.destinationPath,
                            sizeof(s_progress.destinationPath));
    }
    s_operationActive = true;
    notify_progress();
}

static PasteResult finish_operation(PasteResult result) {
    s_progress.result = result;
    s_progress.state = result == PasteResult::Success
        ? OperationState::Completed : OperationState::Failed;
    s_progress.phase = result == PasteResult::Success
        ? PasteStage::Complete : s_lastDiagnostic.stage;
    notify_progress();
    // Keep the callback inside the protected window. It may repaint and pump
    // safe cursor/timer work, but must never be able to start another paste.
    s_operationActive = false;
    fs_fat::set_trash_trace(false);
    if (result == PasteResult::Success) ++s_operationGeneration;
    return result;
}

static void append_text(char* destination, size_t destinationSize, const char* source) {
    if (!destination || destinationSize == 0 || !source) return;
    size_t length = local_strlen(destination);
    while (*source && length + 1 < destinationSize) destination[length++] = *source++;
    destination[length] = '\0';
}

static const char* paste_stage_name_local(PasteStage stage) {
    switch (stage) {
        case PasteStage::None: return "none";
        case PasteStage::ClipboardSet: return "clipboard-population";
        case PasteStage::SourceValidation: return "source-validation";
        case PasteStage::DestinationValidation: return "destination-validation";
        case PasteStage::DestinationNaming: return "destination-naming";
        case PasteStage::SourceOpenRead: return "source-open-read";
        case PasteStage::DestinationCreate: return "destination-create";
        case PasteStage::DataTransfer: return "data-transfer";
        case PasteStage::Flush: return "flush";
        case PasteStage::Verification: return "verification";
        case PasteStage::Refresh: return "refresh";
        case PasteStage::Complete: return "complete";
        default: return "unknown";
    }
}

static const char* paste_result_name_local(PasteResult result) {
    switch (result) {
        case PasteResult::Success: return "SUCCESS";
        case PasteResult::Empty: return "EMPTY";
        case PasteResult::Busy: return "BUSY";
        case PasteResult::SourceMissing: return "SOURCE_MISSING";
        case PasteResult::DestinationMissing: return "DESTINATION_MISSING";
        case PasteResult::ReadOnly: return "READ_ONLY";
        case PasteResult::Conflict: return "CONFLICT";
        case PasteResult::Unsupported: return "UNSUPPORTED";
        case PasteResult::Failed: return "FAILED";
        default: return "FAILED";
    }
}

static void trace_u64(const char* key, uint64_t value) {
    serial::puts(key);
    serial::puts("=");
    serial::put_hex64(value);
    serial::puts("\n");
}

static void trace_status(const char* key, vfs::Status status) {
    serial::puts(key);
    serial::puts("=");
    serial::puts(vfs::status_name(status));
    serial::puts("(");
    serial::put_hex8(static_cast<uint8_t>(status));
    serial::puts(")\n");
}

static void trace_text(const char* key, const char* value) {
    serial::puts(key);
    serial::puts("=");
    serial::puts(value && value[0] ? value : "(empty)");
    serial::puts("\n");
}

static void trace_marker(const char* marker) {
    serial::puts(marker);
    serial::puts("\n");
}

static void trace_transfer(const char* marker, uint64_t offset, uint64_t bytes,
                           const char* status) {
    serial::puts(marker);
    serial::puts(" offset=");
    serial::put_hex64(offset);
    serial::puts(" bytes=");
    serial::put_hex64(bytes);
    serial::puts(" status=");
    serial::puts(status ? status : "unknown");
    serial::puts("\n");
}

static void trace_progress(const char* stage, uint64_t offset, uint64_t expected) {
    ++s_progressCounter;
    serial::puts("FPASTE_PROGRESS stage=");
    serial::puts(stage ? stage : "unknown");
    serial::puts(" sequence=");
    serial::put_hex64(s_progressCounter);
    serial::puts(" offset=");
    serial::put_hex64(offset);
    serial::puts(" expected=");
    serial::put_hex64(expected);
    serial::puts("\n");
}

static void reset_diagnostic(PasteResult result) {
    s_lastDiagnostic = PasteDiagnostic{};
    s_lastDiagnostic.result = result;
}

static void set_diagnostic_stage(PasteStage stage) {
    s_lastDiagnostic.stage = stage;
    if (s_operationActive) s_progress.phase = stage;
    trace_text("DESKTOP_PASTE_STAGE", paste_stage_name_local(stage));
    serial::puts("FPASTE_STAGE=");
    serial::puts(paste_stage_name_local(stage));
    serial::puts("\n");
    if (s_operationActive) notify_progress();
}

static void set_diagnostic_failure(PasteStage stage, PasteResult result,
                                   vfs::Status vfsStatus, const char* fatStatus) {
    s_lastDiagnostic.stage = stage;
    s_lastDiagnostic.result = result;
    s_lastDiagnostic.vfsStatus = vfsStatus;
    copy_text(s_lastDiagnostic.fatStatus, sizeof(s_lastDiagnostic.fatStatus),
              fatStatus ? fatStatus : "");
    trace_text("DESKTOP_PASTE_FAILURE_STAGE", paste_stage_name_local(stage));
    trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(result));
    trace_status("DESKTOP_PASTE_RESULT", vfsStatus);
    if (fatStatus && fatStatus[0]) trace_text("DESKTOP_PASTE_FAT_RESULT", fatStatus);
    if (s_operationActive) {
        s_progress.phase = stage;
        s_progress.result = result;
    }
}

static void copy_diagnostic_path(char* destination, size_t destinationSize, const char* source) {
    copy_text(destination, destinationSize, source ? source : "");
}

static const char* fat_status_for_vfs_status(vfs::Status status) {
    switch (status) {
        case vfs::VFS_ERR_NOT_FOUND: return "FAT_FILE_WRITE_NOT_FOUND";
        case vfs::VFS_ERR_EXISTS: return "FAT_FILE_WRITE_ALREADY_EXISTS";
        case vfs::VFS_ERR_INVALID: return "FAT_FILE_WRITE_INVALID_NAME";
        case vfs::VFS_ERR_NO_SPACE: return "FAT_FILE_WRITE_NO_SPACE";
        case vfs::VFS_ERR_READ_ONLY: return "FAT_FILE_WRITE_READ_ONLY";
        case vfs::VFS_ERR_NOT_MOUNT: return "FAT_FILE_WRITE_NOT_MOUNTED";
        case vfs::VFS_ERR_NOT_SUPPORTED: return "FAT_FILE_WRITE_UNSUPPORTED_TYPE";
        case vfs::VFS_ERR_IO: return "FAT_FILE_WRITE_IO_ERROR";
        case vfs::VFS_ERR_IO_TIMEOUT: return "FAT_FILE_WRITE_IO_TIMEOUT";
        case vfs::VFS_ERR_CORRUPT_CHAIN: return "FAT_FILE_WRITE_CORRUPT_CHAIN";
        case vfs::VFS_ERR_NO_PROGRESS: return "FAT_FILE_WRITE_NO_PROGRESS";
        case vfs::VFS_ERR_ALLOCATION_FAILED: return "FAT_FILE_WRITE_ALLOCATION_FAILED";
        default: return "";
    }
}

static bool append_decimal(char* destination, size_t destinationSize, size_t& length, int value) {
    char digits[12];
    int digitCount = 0;
    if (value <= 0) {
        digits[digitCount++] = '0';
    } else {
        while (value > 0 && digitCount < static_cast<int>(sizeof(digits))) {
            digits[digitCount++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }
    if (length >= destinationSize || static_cast<size_t>(digitCount) >= destinationSize - length) return false;
    for (int i = digitCount - 1; i >= 0; --i) destination[length++] = digits[i];
    destination[length] = '\0';
    return true;
}

static bool is_fat_short_name_char(char c) {
    if (c <= 32 || c >= 127) return false;
    switch (c) {
        case '"': case '*': case '+': case ',': case '/': case ':':
        case ';': case '<': case '=': case '>': case '?': case '[':
        case '\\': case ']': case '|':
            return false;
        default:
            return true;
    }
}

static char uppercase_ascii(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
}

static bool is_fat_short_name(const char* name) {
    if (!name || !name[0]) return false;

    const size_t length = local_strlen(name);
    size_t dotOffset = length;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] != '.') continue;
        if (dotOffset != length) return false;
        dotOffset = i;
    }

    const size_t baseLength = dotOffset;
    const size_t extensionLength = dotOffset < length ? length - dotOffset - 1 : 0;
    if (baseLength == 0 || baseLength > 8 || extensionLength > 3) return false;
    for (size_t i = 0; i < baseLength; ++i) {
        if (!is_fat_short_name_char(name[i])) return false;
    }
    for (size_t i = dotOffset < length ? dotOffset + 1 : length; i < length; ++i) {
        if (!is_fat_short_name_char(name[i]) || name[i] == '.') return false;
    }
    return true;
}

static bool canonical_compare_path(const char* input, char* output, size_t outputSize) {
    if (!input || !output || outputSize == 0) return false;
    char normalized[vfs::VFS_MAX_PATH];
    vfs::normalize_path(input, normalized, sizeof(normalized));
    if (!normalized[0] || !copy_text(output, outputSize, normalized)) return false;

    // Desktop can expose the same FAT tree through an alias mount. Resolve
    // that alias before recursive-destination checks so a display path and
    // its underlying mount path cannot bypass self/descendant rejection.
    const vfs::MountPoint* mount = vfs::get_mount(normalized);
    if (mount && mount->alias && mount->sourcePrefix[0]) {
        size_t mountLength = local_strlen(mount->path);
        size_t normalizedLength = local_strlen(normalized);
        const char* suffix = normalized;
        if (mountLength > 0 && normalizedLength == mountLength) {
            suffix = "";
        } else if (mountLength > 0 && normalizedLength > mountLength &&
                   normalized[mountLength] == '/') {
            suffix = normalized + mountLength + 1;
        } else {
            suffix = nullptr;
        }
        if (suffix) {
            char aliased[vfs::VFS_MAX_PATH];
            vfs::join_path(mount->sourcePrefix, suffix, aliased, sizeof(aliased));
            vfs::normalize_path(aliased, output, outputSize);
        }
    }

    for (size_t i = 0; output[i]; ++i) output[i] = uppercase_ascii(output[i]);
    return true;
}

static bool same_path(const char* left, const char* right) {
    if (!left || !right) return false;
    char normalizedLeft[vfs::VFS_MAX_PATH];
    char normalizedRight[vfs::VFS_MAX_PATH];
    if (!canonical_compare_path(left, normalizedLeft, sizeof(normalizedLeft)) ||
        !canonical_compare_path(right, normalizedRight, sizeof(normalizedRight))) return false;
    size_t leftLength = local_strlen(normalizedLeft);
    size_t rightLength = local_strlen(normalizedRight);
    if (leftLength != rightLength) return false;
    for (size_t i = 0; i < leftLength; ++i) {
        if (normalizedLeft[i] != normalizedRight[i]) return false;
    }
    return true;
}

static bool same_or_descendant_path(const char* path, const char* ancestor) {
    if (!path || !ancestor) return false;
    char normalizedPath[vfs::VFS_MAX_PATH];
    char normalizedAncestor[vfs::VFS_MAX_PATH];
    if (!canonical_compare_path(path, normalizedPath, sizeof(normalizedPath)) ||
        !canonical_compare_path(ancestor, normalizedAncestor, sizeof(normalizedAncestor))) return false;
    size_t pathLength = local_strlen(normalizedPath);
    size_t ancestorLength = local_strlen(normalizedAncestor);
    if (pathLength == ancestorLength) return same_path(normalizedPath, normalizedAncestor);
    if (ancestorLength == 1 && normalizedAncestor[0] == '/') return pathLength > 0 && normalizedPath[0] == '/';
    if (pathLength <= ancestorLength || normalizedPath[ancestorLength] != '/') return false;
    for (size_t i = 0; i < ancestorLength; ++i) {
        if (normalizedPath[i] != normalizedAncestor[i]) return false;
    }
    return true;
}

static bool source_is_valid(vfs::FileInfo* info) {
    if (s_operation == Operation::None || !s_sourcePath[0]) return false;
    vfs::FileInfo localInfo{};
    if (vfs::stat(s_sourcePath, &localInfo) != vfs::VFS_OK) return false;
    if (localInfo.type != vfs::FILE_TYPE_REGULAR && localInfo.type != vfs::FILE_TYPE_DIRECTORY) return false;
    if (info) *info = localInfo;
    return true;
}

static bool destination_is_valid(const char* destinationDirectory, char* normalizedPath, size_t normalizedPathSize) {
    if (!destinationDirectory || !destinationDirectory[0] || !normalizedPath || normalizedPathSize == 0) return false;
    vfs::normalize_path(destinationDirectory, normalizedPath, normalizedPathSize);
    if (!normalizedPath[0]) return false;

    vfs::FileInfo destinationInfo{};
    if (vfs::stat(normalizedPath, &destinationInfo) != vfs::VFS_OK ||
        destinationInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        return false;
    }

    const vfs::MountPoint* mount = vfs::get_mount(normalizedPath);
    return mount && !mount->readOnly && mount->fsType == vfs::FS_TYPE_FAT32;
}

static bool read_directory_entry_at(const char* path, size_t wantedIndex, vfs::DirEntry* out) {
    if (!path || !out) return false;
    uint8_t iterator = vfs::opendir(path);
    if (iterator == 0xFF) return false;

    size_t index = 0;
    vfs::DirEntry entry{};
    while (vfs::readdir(iterator, &entry)) {
        if (entry.name[0] == '.' && (entry.name[1] == '\0' ||
            (entry.name[1] == '.' && entry.name[2] == '\0'))) continue;
        if (index == wantedIndex) {
            const uint8_t* sourceBytes = reinterpret_cast<const uint8_t*>(&entry);
            uint8_t* destinationBytes = reinterpret_cast<uint8_t*>(out);
            for (size_t byteIndex = 0; byteIndex < sizeof(vfs::DirEntry); ++byteIndex) {
                destinationBytes[byteIndex] = sourceBytes[byteIndex];
            }
            vfs::closedir(iterator);
            return true;
        }
        ++index;
    }
    vfs::closedir(iterator);
    return false;
}

static bool make_candidate_name(const char* sourceName, int candidateIndex,
                                char* destinationName, size_t destinationNameSize) {
    if (!sourceName || !destinationName || destinationNameSize == 0 || candidateIndex < 0) return false;
    destinationName[0] = '\0';
    if (candidateIndex == 0 && is_fat_short_name(sourceName)) {
        return copy_text(destinationName, destinationNameSize, sourceName);
    }

    const size_t nameLength = local_strlen(sourceName);
    size_t dotOffset = nameLength;
    for (size_t i = 0; i < nameLength; ++i) {
        if (sourceName[i] == '.') {
            dotOffset = i;
            break;
        }
    }

    char base[9] = {0};
    char extension[4] = {0};
    size_t baseLength = 0;
    size_t extensionLength = 0;
    for (size_t i = 0; i < dotOffset && baseLength < sizeof(base) - 1; ++i) {
        if (is_fat_short_name_char(sourceName[i])) {
            base[baseLength++] = uppercase_ascii(sourceName[i]);
        }
    }
    for (size_t i = dotOffset < nameLength ? dotOffset + 1 : nameLength;
         i < nameLength && extensionLength < sizeof(extension) - 1; ++i) {
        if (is_fat_short_name_char(sourceName[i]) && sourceName[i] != '.') {
            extension[extensionLength++] = uppercase_ascii(sourceName[i]);
        }
    }
    if (baseLength == 0) {
        copy_text(base, sizeof(base), "FILE");
        baseLength = 4;
    }

    char suffix[12] = {0};
    size_t suffixLength = 0;
    if (candidateIndex > 0) {
        suffix[suffixLength++] = '~';
        if (!append_decimal(suffix, sizeof(suffix), suffixLength, candidateIndex)) return false;
    }
    if (suffixLength >= 8) return false;

    const size_t baseCapacity = 8 - suffixLength;
    if (baseLength > baseCapacity) baseLength = baseCapacity;
    size_t outputLength = 0;
    for (size_t i = 0; i < baseLength; ++i) destinationName[outputLength++] = base[i];
    for (size_t i = 0; i < suffixLength; ++i) destinationName[outputLength++] = suffix[i];
    if (extensionLength > 0) {
        destinationName[outputLength++] = '.';
        for (size_t i = 0; i < extensionLength; ++i) destinationName[outputLength++] = extension[i];
    }
    if (outputLength >= destinationNameSize) {
        destinationName[0] = '\0';
        return false;
    }
    destinationName[outputLength] = '\0';
    return true;
}

static bool choose_destination_path(const char* destinationDirectory, char* destinationPath, size_t destinationPathSize) {
    if (!destinationDirectory || !destinationPath || destinationPathSize == 0) return false;
    char candidateName[vfs::VFS_MAX_FILENAME];
    for (int candidateIndex = 0; candidateIndex < kMaxNameCandidates; ++candidateIndex) {
        if (!make_candidate_name(s_sourceName, candidateIndex, candidateName, sizeof(candidateName))) return false;
        vfs::join_path(destinationDirectory, candidateName, destinationPath, destinationPathSize);
        if (!destinationPath[0]) return false;
        if (!vfs::exists(destinationPath)) return true;
    }
    return false;
}

static PasteResult copy_file_contents(const char* sourcePath, const char* destinationPath, uint64_t sourceSize) {
    copy_diagnostic_path(s_lastDiagnostic.sourcePath, sizeof(s_lastDiagnostic.sourcePath), sourcePath);
    copy_diagnostic_path(s_lastDiagnostic.destinationPath, sizeof(s_lastDiagnostic.destinationPath), destinationPath);
    s_lastDiagnostic.bytesExpected = sourceSize;
    trace_u64("DESKTOP_PASTE_BYTES_EXPECTED", sourceSize);
    if (!sourcePath || !destinationPath) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        return PasteResult::Failed;
    }
    if (sourceSize > kMaxFatFileBytes) {
        set_diagnostic_failure(PasteStage::DataTransfer, PasteResult::Unsupported,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_SIZE_OVERFLOW");
        return PasteResult::Unsupported;
    }

    set_operation_state(s_operation == Operation::Move
        ? OperationState::Moving : OperationState::Copying);
    set_diagnostic_stage(PasteStage::SourceOpenRead);
    trace_marker("FPASTE_SOURCE_OPEN");
    trace_text("DESKTOP_PASTE_SOURCE_OPEN", sourcePath);
    const uint8_t sourceHandle = vfs::open(sourcePath, vfs::OPEN_READ);
    if (sourceHandle == 0xFF) {
        set_diagnostic_failure(PasteStage::SourceOpenRead, PasteResult::SourceMissing,
                               vfs::VFS_ERR_NOT_FOUND, "FAT_FILE_WRITE_NOT_FOUND");
        return PasteResult::SourceMissing;
    }
    trace_marker("FPASTE_SOURCE_OPEN_OK");
    trace_text("DESKTOP_PASTE_SOURCE_OPEN_STATUS", "VFS_OK");
    set_diagnostic_stage(PasteStage::DestinationCreate);
    trace_marker("FPASTE_DEST_CREATE");
    trace_text("DESKTOP_PASTE_DEST_CREATE", destinationPath);
    trace_u64("LFPASTE_COPY_BUFFER_BYTES", kCopyBufferBytes);
    trace_u64("LFPASTE_BEGIN_SIZE", sourceSize);
    const int32_t emptyCreate = vfs::create_file(destinationPath, nullptr, 0);
    if (emptyCreate != 0) {
        vfs::close(sourceHandle);
        set_diagnostic_failure(PasteStage::DestinationCreate, PasteResult::Failed,
                               emptyCreate < 0 ? static_cast<vfs::Status>(emptyCreate)
                                               : vfs::VFS_ERR_IO,
                               fs_fat::traversal_status_name(fs_fat::last_traversal_status()));
        return PasteResult::Failed;
    }
    const uint8_t destinationHandle = vfs::open(destinationPath, vfs::OPEN_WRITE);
    if (destinationHandle == 0xFF) {
        vfs::close(sourceHandle);
        vfs::unlink(destinationPath);
        set_diagnostic_failure(PasteStage::DestinationCreate, PasteResult::Failed,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_OPEN_DESTINATION");
        return PasteResult::Failed;
    }
    trace_marker("FPASTE_DEST_CREATE_OK");

    uint64_t sourceOffset = 0;
    uint64_t destinationOffset = 0;
    uint64_t lastProgressNotification = s_progress.bytesCompleted;
    bool transferOk = true;
    vfs::Status transferStatus = vfs::VFS_OK;
    PasteStage failureStage = PasteStage::DataTransfer;
    set_diagnostic_stage(PasteStage::DataTransfer);
    while (sourceOffset < sourceSize) {
        const uint64_t remaining = sourceSize - sourceOffset;
        const uint32_t requested = remaining > kCopyBufferBytes
            ? kCopyBufferBytes : static_cast<uint32_t>(remaining);
        serial::puts("LFPASTE_READ offset=0x");
        serial::put_hex64(sourceOffset);
        serial::puts(" requested=0x");
        serial::put_hex32(requested);
        serial::puts(" buffer=0x");
        serial::put_hex64(reinterpret_cast<uintptr_t>(s_copyBuffer));
        serial::puts("\n");
        const int32_t actualRead = vfs::read(sourceHandle, s_copyBuffer, requested);
        trace_transfer("LFPASTE_READ_END", sourceOffset,
                       actualRead > 0 ? static_cast<uint64_t>(actualRead) : 0,
                       fs_fat::traversal_status_name(fs_fat::last_traversal_status()));
        if (actualRead != static_cast<int32_t>(requested)) {
            transferOk = false;
            transferStatus = actualRead < 0 ? static_cast<vfs::Status>(actualRead)
                                             : vfs::VFS_ERR_IO;
            failureStage = PasteStage::SourceOpenRead;
            break;
        }
        s_lastDiagnostic.bytesRead += static_cast<uint64_t>(actualRead);
        sourceOffset += static_cast<uint64_t>(actualRead);

        uint32_t bufferOffset = 0;
        while (bufferOffset < static_cast<uint32_t>(actualRead)) {
            const uint32_t writeRequest = static_cast<uint32_t>(actualRead) - bufferOffset;
            serial::puts("LFPASTE_WRITE offset=0x");
            serial::put_hex64(destinationOffset);
            serial::puts(" requested=0x");
            serial::put_hex32(writeRequest);
            serial::puts(" read=0x");
            serial::put_hex32(actualRead);
            serial::puts("\n");
            const int32_t actualWrite = vfs::write(destinationHandle,
                                                   s_copyBuffer + bufferOffset,
                                                   writeRequest);
            trace_transfer("LFPASTE_WRITE_END", destinationOffset,
                           actualWrite > 0 ? static_cast<uint64_t>(actualWrite) : 0,
                           fs_fat::traversal_status_name(fs_fat::last_traversal_status()));
            if (actualWrite <= 0 || static_cast<uint32_t>(actualWrite) > writeRequest) {
                transferOk = false;
                transferStatus = actualWrite < 0 ? static_cast<vfs::Status>(actualWrite)
                                                  : vfs::VFS_ERR_NO_PROGRESS;
                failureStage = PasteStage::DataTransfer;
                break;
            }
            bufferOffset += static_cast<uint32_t>(actualWrite);
            destinationOffset += static_cast<uint64_t>(actualWrite);
            s_lastDiagnostic.bytesWritten = destinationOffset;
            s_progress.bytesCompleted += static_cast<uint64_t>(actualWrite);
        }
        if (!transferOk) break;
        if (sourceOffset >= s_lastDiagnostic.bytesRead &&
            (destinationOffset == sourceOffset || destinationOffset % (64u * 1024u) == 0)) {
            trace_progress("stream", destinationOffset, sourceSize);
        }
        if (s_progress.bytesCompleted - lastProgressNotification >= kCopyBufferBytes ||
            sourceOffset >= sourceSize) {
            lastProgressNotification = s_progress.bytesCompleted;
            notify_progress();
        }
    }

    const vfs::Status sourceCloseStatus = vfs::close(sourceHandle);
    trace_marker("FPASTE_CLOSE_SOURCE");
    trace_status("DESKTOP_PASTE_SOURCE_CLOSE", sourceCloseStatus);
    if (sourceCloseStatus != vfs::VFS_OK && transferOk) {
        transferOk = false;
        transferStatus = sourceCloseStatus;
        failureStage = PasteStage::SourceOpenRead;
    }
    if (transferOk) {
        set_diagnostic_stage(PasteStage::Flush);
        trace_marker("LFPASTE_FLUSH_BEGIN");
        const vfs::Status flushStatus = vfs::flush(destinationHandle);
        trace_status("LFPASTE_FLUSH_END", flushStatus);
        if (flushStatus != vfs::VFS_OK) {
            transferOk = false;
            transferStatus = flushStatus;
            failureStage = PasteStage::Flush;
        }
    }
    const vfs::Status destinationCloseStatus = vfs::close(destinationHandle);
    trace_marker("FPASTE_CLOSE_DEST");
    trace_status("DESKTOP_PASTE_DEST_CLOSE", destinationCloseStatus);
    if (destinationCloseStatus != vfs::VFS_OK && transferOk) {
        transferOk = false;
        transferStatus = destinationCloseStatus;
        failureStage = PasteStage::Flush;
    }
    if (!transferOk) {
        const vfs::Status cleanupStatus = vfs::unlink(destinationPath);
        trace_status("DESKTOP_PASTE_ROLLBACK", cleanupStatus);
        set_diagnostic_failure(failureStage, PasteResult::Failed,
                               transferStatus, fat_status_for_vfs_status(transferStatus));
        return PasteResult::Failed;
    }

    set_operation_state(OperationState::Verifying);
    set_diagnostic_stage(PasteStage::Verification);
    trace_marker("FPASTE_VERIFY_BEGIN");
    vfs::FileInfo destinationInfo{};
    const vfs::Status verifyStatus = vfs::stat(destinationPath, &destinationInfo);
    trace_status("DESKTOP_PASTE_VERIFY_STATUS", verifyStatus);
    trace_u64("DESKTOP_PASTE_VERIFY_SIZE", destinationInfo.size);
    if (verifyStatus != vfs::VFS_OK || destinationInfo.type != vfs::FILE_TYPE_REGULAR ||
        destinationInfo.size != sourceSize) {
        const vfs::Status failureStatus = verifyStatus == vfs::VFS_OK
            ? vfs::VFS_ERR_IO : verifyStatus;
        const vfs::Status cleanupStatus = vfs::unlink(destinationPath);
        trace_status("DESKTOP_PASTE_ROLLBACK", cleanupStatus);
        set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                               failureStatus, fat_status_for_vfs_status(failureStatus));
        return PasteResult::Failed;
    }
    trace_marker("FPASTE_VERIFY_END");
    trace_text("DESKTOP_PASTE_VERIFY", "VFS_OK size-match");
    serial::puts("LFPASTE_COMPLETE bytes=0x");
    serial::put_hex64(destinationOffset);
    serial::puts("\n");
    return PasteResult::Success;
}

static PasteResult remove_entry_tree(const char* path, uint32_t depth) {
    if (!path || !path[0]) return PasteResult::Failed;
    if (depth > kMaxTreeDepth) {
        trace_marker("FPASTE_RECURSION_DEPTH_LIMIT");
        return PasteResult::Unsupported;
    }

    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK) return PasteResult::Success;
    if (info.type == vfs::FILE_TYPE_REGULAR) {
        return vfs::unlink(path) == vfs::VFS_OK ? PasteResult::Success : PasteResult::Failed;
    }
    if (info.type != vfs::FILE_TYPE_DIRECTORY) return PasteResult::Failed;

    for (size_t removedCount = 0;
         removedCount < kMaxDirectoryEntriesPerOperation; ++removedCount) {
        vfs::DirEntry child{};
        if (!read_directory_entry_at(path, 0, &child)) break;
        char childPath[vfs::VFS_MAX_PATH];
        vfs::join_path(path, child.name, childPath, sizeof(childPath));
        if (!childPath[0] || remove_entry_tree(childPath, depth + 1) != PasteResult::Success) {
            return PasteResult::Failed;
        }
    }
    vfs::DirEntry remainingChild{};
    if (read_directory_entry_at(path, 0, &remainingChild)) {
        trace_marker("FPASTE_ROLLBACK_ENTRY_LIMIT");
        return PasteResult::Unsupported;
    }
    return vfs::rmdir(path) == vfs::VFS_OK ? PasteResult::Success : PasteResult::Failed;
}

static PasteResult copy_entry_tree(const char* sourcePath, const char* destinationPath,
                                   const vfs::FileInfo& sourceInfo, uint32_t depth) {
    if (!sourcePath || !destinationPath) return PasteResult::Failed;
    if (depth > kMaxTreeDepth) {
        trace_marker("FPASTE_RECURSION_DEPTH_LIMIT");
        return PasteResult::Unsupported;
    }
    if (sourceInfo.type == vfs::FILE_TYPE_REGULAR) {
        return copy_file_contents(sourcePath, destinationPath, sourceInfo.size);
    }
    if (sourceInfo.type != vfs::FILE_TYPE_DIRECTORY) return PasteResult::Unsupported;

    if (vfs::mkdir(destinationPath) != vfs::VFS_OK) return PasteResult::Failed;
    for (size_t childIndex = 0; childIndex < kMaxDirectoryEntriesPerOperation; ++childIndex) {
        vfs::DirEntry child{};
        if (!read_directory_entry_at(sourcePath, childIndex, &child)) break;

        char childSourcePath[vfs::VFS_MAX_PATH];
        char childDestinationPath[vfs::VFS_MAX_PATH];
        vfs::join_path(sourcePath, child.name, childSourcePath, sizeof(childSourcePath));
        vfs::join_path(destinationPath, child.name, childDestinationPath, sizeof(childDestinationPath));
        if (!childSourcePath[0] || !childDestinationPath[0]) {
            remove_entry_tree(destinationPath, depth);
            return PasteResult::Failed;
        }

        vfs::FileInfo childInfo{};
        if (vfs::stat(childSourcePath, &childInfo) != vfs::VFS_OK) {
            remove_entry_tree(destinationPath, depth);
            return PasteResult::SourceMissing;
        }
        PasteResult childResult = copy_entry_tree(childSourcePath, childDestinationPath,
                                                  childInfo, depth + 1);
        if (childResult != PasteResult::Success) {
            remove_entry_tree(destinationPath, depth);
            return childResult;
        }
    }
    if (kMaxDirectoryEntriesPerOperation == 0) return PasteResult::Failed;
    // A directory with no end after the bounded enumeration is corrupt or
    // unsupported for this release; never keep synchronously enumerating it.
    vfs::DirEntry endProbe{};
    if (read_directory_entry_at(sourcePath, kMaxDirectoryEntriesPerOperation, &endProbe)) {
        remove_entry_tree(destinationPath, depth);
        return PasteResult::Unsupported;
    }
    return PasteResult::Success;
}

static bool verify_entry_tree(const char* sourcePath, const char* destinationPath,
                              const vfs::FileInfo& sourceInfo, uint32_t depth) {
    if (!sourcePath || !destinationPath || depth > kMaxTreeDepth) return false;
    vfs::FileInfo destinationInfo{};
    if (vfs::stat(destinationPath, &destinationInfo) != vfs::VFS_OK ||
        destinationInfo.type != sourceInfo.type) return false;
    if (sourceInfo.type == vfs::FILE_TYPE_REGULAR) {
        return destinationInfo.size == sourceInfo.size;
    }
    if (sourceInfo.type != vfs::FILE_TYPE_DIRECTORY) return false;

    for (size_t childIndex = 0; childIndex < kMaxDirectoryEntriesPerOperation; ++childIndex) {
        vfs::DirEntry child{};
        if (!read_directory_entry_at(sourcePath, childIndex, &child)) break;
        char childSourcePath[vfs::VFS_MAX_PATH];
        char childDestinationPath[vfs::VFS_MAX_PATH];
        vfs::join_path(sourcePath, child.name, childSourcePath, sizeof(childSourcePath));
        vfs::join_path(destinationPath, child.name, childDestinationPath,
                       sizeof(childDestinationPath));
        if (!childSourcePath[0] || !childDestinationPath[0]) return false;
        vfs::FileInfo childInfo{};
        if (vfs::stat(childSourcePath, &childInfo) != vfs::VFS_OK ||
            !verify_entry_tree(childSourcePath, childDestinationPath, childInfo, depth + 1)) {
            return false;
        }
    }
    return true;
}

static bool make_new_folder_name(int suffixIndex, char* out, size_t outSize) {
    if (!out || outSize == 0 || suffixIndex <= 0) return false;
    // FAT directory creation currently supports short 8.3 names only. Keep
    // the existing NewFolder naming policy in the largest representable form
    // and reserve the final characters for collision suffixes.
    const char* base = "NewFolde";
    if (suffixIndex == 1) return copy_text(out, outSize, base);

    char suffix[12] = {0};
    size_t suffixLength = 0;
    if (!append_decimal(suffix, sizeof(suffix), suffixLength, suffixIndex)) return false;
    const size_t baseLength = suffixLength >= 8 ? 1 : 8 - suffixLength;
    if (baseLength + suffixLength >= outSize) return false;
    for (size_t i = 0; i < baseLength; ++i) out[i] = base[i];
    for (size_t i = 0; i < suffixLength; ++i) out[baseLength + i] = suffix[i];
    out[baseLength + suffixLength] = '\0';
    return true;
}

static bool path_matches_mount(const char* path, const char* mountPath) {
    if (!path || !mountPath || !mountPath[0]) return false;
    const size_t mountLength = local_strlen(mountPath);
    for (size_t i = 0; i < mountLength; ++i) {
        if (path[i] != mountPath[i]) return false;
    }
    return path[mountLength] == '\0' || path[mountLength] == '/' ||
        (mountLength == 1 && mountPath[0] == '/');
}

static const vfs::MountPoint* mount_for_path(const char* path) {
    const vfs::MountPoint* best = nullptr;
    size_t bestLength = 0;
    for (uint8_t i = 0; i < vfs::VFS_MAX_MOUNTS; ++i) {
        const vfs::MountPoint* mount = vfs::get_mount_by_index(i);
        if (!mount || !mount->active || !path_matches_mount(path, mount->path)) continue;
        const size_t length = local_strlen(mount->path);
        if (!best || length > bestLength) {
            best = mount;
            bestLength = length;
        }
    }
    return best;
}

static bool trash_root_for_path(const char* sourcePath, char* outPath, size_t outPathSize) {
    if (!outPath || outPathSize == 0) return false;
    outPath[0] = '\0';
    const vfs::MountPoint* mount = mount_for_path(sourcePath);
    if (!mount) return false;
    if (local_strlen(mount->path) == 1 && mount->path[0] == '/') {
        return copy_text(outPath, outPathSize, kTrashRootPath);
    }
    if (!copy_text(outPath, outPathSize, mount->path)) return false;
    append_text(outPath, outPathSize, "/Trash");
    return outPath[0] != '\0';
}

static bool make_trash_candidate_name(const char* sourceName, int suffixIndex, bool isDirectory,
                                      char* outName, size_t outNameSize) {
    (void)isDirectory;
    if (!make_candidate_name(sourceName, suffixIndex, outName, outNameSize)) return false;
    // Keep the reserved metadata namespace out of the data namespace. This
    // prevents a deleted user file from being mistaken for restore metadata.
    return !is_trash_metadata_name(outName);
}

static bool trash_candidate_is_free(const char* candidatePath) {
    if (!candidatePath || !candidatePath[0]) return false;
    vfs::FileInfo info{};
    if (vfs::stat(candidatePath, &info) == vfs::VFS_OK) return false;
    char infoPath[vfs::VFS_MAX_PATH] = {0};
    if (!trash_metadata_path_for(candidatePath, infoPath, sizeof(infoPath))) return false;
    return vfs::stat(infoPath, &info) != vfs::VFS_OK;
}

static bool choose_trash_path(const char* trashRoot, const char* sourceName,
                              bool isDirectory, char* outPath, size_t outPathSize) {
    if (!trashRoot || !sourceName || !outPath || outPathSize == 0) return false;
    for (int suffixIndex = 0; suffixIndex < kMaxNameCandidates; ++suffixIndex) {
        char candidateName[vfs::VFS_MAX_FILENAME] = {0};
        if (!make_trash_candidate_name(sourceName, suffixIndex, isDirectory,
                                       candidateName, sizeof(candidateName))) continue;
        char candidatePath[vfs::VFS_MAX_PATH] = {0};
        vfs::join_path(trashRoot, candidateName, candidatePath, sizeof(candidatePath));
        if (trash_candidate_is_free(candidatePath) && copy_text(outPath, outPathSize, candidatePath)) return true;
    }
    return false;
}

static bool write_trash_metadata(const char* trashedPath, const char* sourcePath,
                                 bool isDirectory, vfs::Status* outStatus) {
    if (outStatus) *outStatus = vfs::VFS_ERR_INVALID;
    char infoPath[vfs::VFS_MAX_PATH] = {0};
    if (!trash_metadata_path_for(trashedPath, infoPath, sizeof(infoPath))) return false;
    trace_text("TRASH_METADATA_PATH", infoPath);
    trace_marker("TRASH_METADATA_CREATE_BEGIN");
    trace_marker("TRASH_FAT_DIR_ENTRY_BEGIN");

    char metadata[512] = {0};
    // FAT desktop names cannot contain quotes, so the bounded metadata format
    // is safe here while retaining the original path for restore.
    append_text(metadata, sizeof(metadata), "{\n  \"originalPath\": \"");
    append_text(metadata, sizeof(metadata), sourcePath);
    append_text(metadata, sizeof(metadata), "\",\n  \"originalName\": \"");
    append_text(metadata, sizeof(metadata), vfs::basename(sourcePath));
    append_text(metadata, sizeof(metadata), "\",\n  \"isDirectory\": ");
    append_text(metadata, sizeof(metadata), isDirectory ? "true" : "false");
    append_text(metadata, sizeof(metadata), "\n}");
    const int32_t written = vfs::create_file(infoPath, metadata,
                                             static_cast<uint32_t>(local_strlen(metadata)));
    if (written < 0 || written != static_cast<int32_t>(local_strlen(metadata))) {
        if (outStatus) {
            *outStatus = written < 0 ? static_cast<vfs::Status>(written) : vfs::VFS_ERR_IO;
        }
        const vfs::Status status = outStatus ? *outStatus : vfs::VFS_ERR_IO;
        trace_status("TRASH_FAT_DIR_ENTRY_RESULT", status);
        trace_status("TRASH_METADATA_CREATE_RESULT", status);
        return false;
    }
    if (outStatus) *outStatus = vfs::VFS_OK;
    trace_status("TRASH_FAT_DIR_ENTRY_RESULT", vfs::VFS_OK);
    trace_status("TRASH_METADATA_CREATE_RESULT", vfs::VFS_OK);
    return true;
}

static void remove_trash_metadata(const char* trashedPath) {
    char infoPath[vfs::VFS_MAX_PATH] = {0};
    if (trash_metadata_path_for(trashedPath, infoPath, sizeof(infoPath))) vfs::unlink(infoPath);
}

static uint32_t trash_name_hash(const char* name) {
    uint32_t hash = 2166136261u;
    if (!name) return hash;
    for (size_t i = 0; name[i]; ++i) {
        hash ^= static_cast<uint8_t>(uppercase_ascii(name[i]));
        hash *= 16777619u;
    }
    return hash;
}

static char hex_digit(uint32_t value) {
    value &= 0xFu;
    return value < 10u ? static_cast<char>('0' + value)
                       : static_cast<char>('A' + value - 10u);
}

bool is_trash_metadata_name(const char* name) {
    if (!name) return false;
    // TRxxxxxx.INF is an intentionally reserved FAT 8.3 namespace. It is
    // paired with the data entry by hash and never uses a dot-prefixed LFN.
    const size_t length = local_strlen(name);
    if (length != 12 || uppercase_ascii(name[0]) != 'T' || uppercase_ascii(name[1]) != 'R' || name[8] != '.') return false;
    if (uppercase_ascii(name[9]) != 'I' || uppercase_ascii(name[10]) != 'N' || uppercase_ascii(name[11]) != 'F') return false;
    for (size_t i = 2; i < 8; ++i) {
        const char c = uppercase_ascii(name[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool trash_metadata_path_for(const char* trashedPath,
                             char* outMetadataPath,
                             size_t outMetadataPathSize) {
    if (!trashedPath || !trashedPath[0] || !outMetadataPath || outMetadataPathSize == 0) return false;
    outMetadataPath[0] = '\0';
    const char* itemName = vfs::basename(trashedPath);
    if (!itemName || !itemName[0]) return false;

    char metadataName[13] = {0};
    const uint32_t hash = trash_name_hash(itemName);
    metadataName[0] = kTrashMetadataPrefix[0];
    metadataName[1] = kTrashMetadataPrefix[1];
    for (uint32_t digit = 0; digit < 6; ++digit) {
        const uint32_t shift = (5u - digit) * 4u;
        metadataName[2 + digit] = hex_digit(hash >> shift);
    }
    metadataName[8] = kTrashMetadataExtension[0];
    metadataName[9] = kTrashMetadataExtension[1];
    metadataName[10] = kTrashMetadataExtension[2];
    metadataName[11] = kTrashMetadataExtension[3];
    metadataName[12] = '\0';

    char parentPath[vfs::VFS_MAX_PATH] = {0};
    vfs::parent_path(trashedPath, parentPath, sizeof(parentPath));
    vfs::join_path(parentPath, metadataName, outMetadataPath, outMetadataPathSize);
    return outMetadataPath[0] != '\0';
}

bool set_file(const char* sourcePath, Operation operation) {
    if (s_operationActive) {
        trace_marker("FILE_CLIPBOARD_REJECTED_BUSY");
        return false;
    }
    reset_diagnostic(PasteResult::Failed);
    set_diagnostic_stage(PasteStage::ClipboardSet);
    trace_text("FILE_CLIPBOARD_SOURCE_REQUEST", sourcePath);
    if (!sourcePath || !sourcePath[0] || operation == Operation::None) {
        set_diagnostic_failure(PasteStage::ClipboardSet, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        return false;
    }

    char normalizedPath[vfs::VFS_MAX_PATH];
    vfs::normalize_path(sourcePath, normalizedPath, sizeof(normalizedPath));
    trace_text("FILE_CLIPBOARD_SOURCE", normalizedPath);
    vfs::FileInfo info{};
    const vfs::Status sourceStatus = normalizedPath[0]
        ? vfs::stat(normalizedPath, &info) : vfs::VFS_ERR_INVALID;
    trace_status("FILE_CLIPBOARD_SOURCE_STAT", sourceStatus);
    if (!normalizedPath[0] || sourceStatus != vfs::VFS_OK ||
        (info.type != vfs::FILE_TYPE_REGULAR && info.type != vfs::FILE_TYPE_DIRECTORY)) {
        set_diagnostic_failure(PasteStage::ClipboardSet, PasteResult::SourceMissing,
                               sourceStatus, "FAT_FILE_WRITE_NOT_FOUND");
        return false;
    }

    const char* name = vfs::basename(normalizedPath);
    if (!name || !name[0]) {
        set_diagnostic_failure(PasteStage::ClipboardSet, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_NAME");
        return false;
    }
    char nextSourcePath[vfs::VFS_MAX_PATH];
    char nextSourceName[vfs::VFS_MAX_FILENAME];
    if (!copy_text(nextSourcePath, sizeof(nextSourcePath), normalizedPath) ||
        !copy_text(nextSourceName, sizeof(nextSourceName), name)) {
        set_diagnostic_failure(PasteStage::ClipboardSet, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        return false;
    }

    copy_text(s_sourcePath, sizeof(s_sourcePath), nextSourcePath);
    copy_text(s_sourceName, sizeof(s_sourceName), nextSourceName);
    s_operation = operation;
    s_sourceSize = info.type == vfs::FILE_TYPE_REGULAR ? info.size : 0;
    s_sourceIsRegularFile = info.type == vfs::FILE_TYPE_REGULAR;
    trace_text("FILE_CLIPBOARD_COPY_SET", operation == Operation::Copy ? "COPY" : "MOVE");
    trace_text("FILE_CLIPBOARD_SOURCE_TYPE", info.type == vfs::FILE_TYPE_REGULAR ? "REGULAR_FILE" : "DIRECTORY");
    trace_text("FILE_CLIPBOARD_SOURCE_NAME", s_sourceName);
    return true;
}

void clear() {
    s_sourcePath[0] = '\0';
    s_sourceName[0] = '\0';
    s_operation = Operation::None;
    s_sourceSize = 0;
    s_sourceIsRegularFile = false;
}

bool has_pending_file() {
    return s_operation != Operation::None && s_sourcePath[0] && s_sourceName[0];
}

Operation pending_operation() {
    return s_operation;
}

void set_progress_callback(ProgressCallback callback) {
    s_progressCallback = callback;
}

bool operation_active() {
    return s_operationActive;
}

OperationState operation_state() {
    return s_progress.state;
}

const FileOperationProgress& progress() {
    return s_progress;
}

uint64_t operation_generation() {
    return s_operationGeneration;
}

bool can_paste_to(const char* destinationDirectory) {
    if (s_operationActive) return false;
    vfs::FileInfo sourceInfo{};
    if (!source_is_valid(&sourceInfo)) {
        clear();
        return false;
    }
    char normalizedDestination[vfs::VFS_MAX_PATH];
    if (!destination_is_valid(destinationDirectory, normalizedDestination, sizeof(normalizedDestination))) return false;
    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY &&
        same_or_descendant_path(normalizedDestination, s_sourcePath)) return false;
    return true;
}

PasteResult paste_to_directory(const char* destinationDirectory) {
    if (s_operationActive) return PasteResult::Busy;
    reset_diagnostic(PasteResult::Failed);
    s_progressCounter = 0;
    serial::puts("FPASTE_BEGIN identity=");
    serial::puts(GXOS_BUILD_IDENTITY);
    serial::puts(" probe=");
    serial::puts(GXOS_BUILD_PROBE_ID);
    serial::puts("\n");
    serial::puts("DESKTOP_PASTE_BEGIN\n");
    if (!has_pending_file()) {
        set_diagnostic_stage(PasteStage::SourceValidation);
        s_lastDiagnostic.result = PasteResult::Empty;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Empty));
        trace_marker("FPASTE_RETURN_EMPTY");
        return PasteResult::Empty;
    }
    begin_operation_progress(destinationDirectory);
    trace_marker("FPASTE_CLIPBOARD_VALID");

    copy_diagnostic_path(s_lastDiagnostic.sourcePath, sizeof(s_lastDiagnostic.sourcePath), s_sourcePath);
    trace_text("DESKTOP_PASTE_SOURCE", s_sourcePath);
    trace_text("DESKTOP_PASTE_SOURCE_NAME", s_sourceName);
    trace_text("DESKTOP_PASTE_OPERATION", s_operation == Operation::Copy ? "COPY" : "MOVE");

    set_diagnostic_stage(PasteStage::SourceValidation);
    vfs::FileInfo sourceInfo{};
    const vfs::Status sourceStatus = vfs::stat(s_sourcePath, &sourceInfo);
    trace_status("DESKTOP_PASTE_SOURCE_STAT", sourceStatus);
    trace_text("DESKTOP_PASTE_SOURCE_TYPE",
               sourceInfo.type == vfs::FILE_TYPE_REGULAR ? "REGULAR_FILE" :
               sourceInfo.type == vfs::FILE_TYPE_DIRECTORY ? "DIRECTORY" : "OTHER");
    trace_u64("DESKTOP_PASTE_SOURCE_SIZE", sourceInfo.size);
    s_progress.totalKnown = sourceInfo.type == vfs::FILE_TYPE_REGULAR;
    s_progress.totalBytes = s_progress.totalKnown ? sourceInfo.size : 0;
    notify_progress();
    if (sourceStatus != vfs::VFS_OK ||
        (sourceInfo.type != vfs::FILE_TYPE_REGULAR && sourceInfo.type != vfs::FILE_TYPE_DIRECTORY)) {
        clear();
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::SourceMissing,
                               sourceStatus == vfs::VFS_OK ? vfs::VFS_ERR_INVALID : sourceStatus,
                               "FAT_FILE_WRITE_NOT_FOUND");
        return finish_operation(PasteResult::SourceMissing);
    }
    trace_marker("FPASTE_SOURCE_RESOLVED");

    char normalizedDestination[vfs::VFS_MAX_PATH] = {0};
    if (destinationDirectory && destinationDirectory[0]) {
        vfs::normalize_path(destinationDirectory, normalizedDestination, sizeof(normalizedDestination));
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationDirectory,
                         sizeof(s_lastDiagnostic.destinationDirectory), normalizedDestination);
    copy_diagnostic_path(s_progress.destinationPath, sizeof(s_progress.destinationPath), normalizedDestination);
    trace_text("DESKTOP_PASTE_DEST_DIR", normalizedDestination);

    set_diagnostic_stage(PasteStage::DestinationValidation);
    vfs::FileInfo destinationInfo{};
    const vfs::Status destinationStatus = normalizedDestination[0]
        ? vfs::stat(normalizedDestination, &destinationInfo) : vfs::VFS_ERR_INVALID;
    trace_status("DESKTOP_PASTE_DEST_DIR_STAT", destinationStatus);
    if (destinationStatus != vfs::VFS_OK || destinationInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        const vfs::Status failureStatus = destinationStatus != vfs::VFS_OK
            ? destinationStatus : vfs::VFS_ERR_NOT_DIR;
        const PasteResult result = failureStatus == vfs::VFS_ERR_READ_ONLY
            ? PasteResult::ReadOnly : PasteResult::DestinationMissing;
        set_diagnostic_failure(PasteStage::DestinationValidation, result, failureStatus,
                               fat_status_for_vfs_status(failureStatus));
        return finish_operation(result);
    }
    trace_marker("FPASTE_FOLDER_RESOLVED");

    const vfs::MountPoint* destinationMount = vfs::get_mount(normalizedDestination);
    if (!destinationMount) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::DestinationMissing,
                               vfs::VFS_ERR_NOT_MOUNT, "FAT_FILE_WRITE_NOT_MOUNTED");
        return finish_operation(PasteResult::DestinationMissing);
    }
    serial::puts("DESKTOP_PASTE_DEST_FS=");
    serial::puts(vfs::fs_type_name(destinationMount->fsType));
    serial::puts(" mount=");
    serial::puts(destinationMount->path);
    serial::puts(" source=");
    serial::puts(destinationMount->sourcePrefix[0] ? destinationMount->sourcePrefix : "/");
    serial::puts(" alias=");
    serial::puts(destinationMount->alias ? "1" : "0");
    serial::puts(" writable=");
    serial::puts(destinationMount->readOnly ? "0\n" : "1\n");
    if (destinationMount->readOnly) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::ReadOnly,
                               vfs::VFS_ERR_READ_ONLY, "FAT_FILE_WRITE_READ_ONLY");
        return finish_operation(PasteResult::ReadOnly);
    }
    if (destinationMount->fsType != vfs::FS_TYPE_FAT32) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_NOT_SUPPORTED, "FAT_FILE_WRITE_UNSUPPORTED_TYPE");
        return finish_operation(PasteResult::Unsupported);
    }

    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY) {
        trace_marker("FPASTE_RECURSION_CHECK_BEGIN");
    }
    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY &&
        same_or_descendant_path(normalizedDestination, s_sourcePath)) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        trace_marker("FPASTE_RECURSION_CHECK_END rejected");
        return finish_operation(PasteResult::Unsupported);
    }
    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY) {
        trace_marker("FPASTE_RECURSION_CHECK_END accepted");
    }

    set_diagnostic_stage(PasteStage::DestinationNaming);
    char destinationPath[vfs::VFS_MAX_PATH];
    if (!choose_destination_path(normalizedDestination, destinationPath, sizeof(destinationPath))) {
        set_diagnostic_failure(PasteStage::DestinationNaming, PasteResult::Conflict,
                               vfs::VFS_ERR_EXISTS, "FAT_FILE_WRITE_ALREADY_EXISTS");
        return finish_operation(PasteResult::Conflict);
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationPath,
                         sizeof(s_lastDiagnostic.destinationPath), destinationPath);
    copy_diagnostic_path(s_progress.destinationPath, sizeof(s_progress.destinationPath), destinationPath);
    trace_text("DESKTOP_PASTE_DEST_NAME", vfs::basename(destinationPath));
    trace_text("DESKTOP_PASTE_DEST_PATH", destinationPath);
    trace_marker("FPASTE_DEST_NAME_READY");

    // Cutting an item into its current directory is a safe no-op. Copying to
    // the same directory deliberately takes the deterministic Copy name path.
    char sourceDestinationPath[vfs::VFS_MAX_PATH];
    vfs::join_path(normalizedDestination, s_sourceName,
                   sourceDestinationPath, sizeof(sourceDestinationPath));
    if (s_operation == Operation::Move && same_path(s_sourcePath, sourceDestinationPath)) {
        clear();
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
        return finish_operation(PasteResult::Success);
    }

    if (s_operation == Operation::Move) {
        const vfs::MountPoint* sourceMount = vfs::get_mount(s_sourcePath);
        destinationMount = vfs::get_mount(destinationPath);
        if (!sourceMount || !destinationMount) {
            set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::DestinationMissing,
                                   vfs::VFS_ERR_NOT_MOUNT, "FAT_FILE_WRITE_NOT_MOUNTED");
            return finish_operation(PasteResult::DestinationMissing);
        }
        if (sourceMount->readOnly) {
            set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::ReadOnly,
                                   vfs::VFS_ERR_READ_ONLY, "FAT_FILE_WRITE_READ_ONLY");
            return finish_operation(PasteResult::ReadOnly);
        }

        // Use copy-then-verify-delete for every clipboard Move. This keeps
        // Cut semantics identical across filesystems and avoids depending on
        // an atomic same-volume rename to publish two directory entries. The
        // source is never deleted until the destination tree is verified.
        set_operation_state(OperationState::Moving);
        PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo, 0);
        if (copied != PasteResult::Success) return finish_operation(copied);
        set_operation_state(OperationState::Verifying);
        set_diagnostic_stage(PasteStage::Verification);
        trace_marker("FPASTE_VERIFY_BEGIN");
        if (!verify_entry_tree(s_sourcePath, destinationPath, sourceInfo, 0)) {
            trace_marker("FPASTE_VERIFY_END status=FAILED");
            remove_entry_tree(destinationPath, 0);
            set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                                   vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
            return finish_operation(PasteResult::Failed);
        }
        trace_marker("FPASTE_VERIFY_END status=OK");
        PasteResult removed = remove_entry_tree(s_sourcePath, 0);
        if (removed != PasteResult::Success) {
            remove_entry_tree(destinationPath, 0);
            set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                                   vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
            return finish_operation(PasteResult::Failed);
        }
        clear();
        trace_marker("FPASTE_CLIPBOARD_FINALIZE");
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
        trace_marker("FPASTE_OPERATION_COMPLETE");
        return finish_operation(PasteResult::Success);
    }

    trace_marker("FPASTE_COPY_BEGIN");
    set_operation_state(OperationState::Copying);
    PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo, 0);
    if (copied == PasteResult::Success) {
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
        trace_marker("FPASTE_CLIPBOARD_FINALIZE");
        trace_marker("FPASTE_OPERATION_COMPLETE");
    } else if (s_lastDiagnostic.stage == PasteStage::None) {
        set_diagnostic_failure(PasteStage::DataTransfer, copied,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
    }
    return finish_operation(copied);
}

static void log_folder_status(const char* key, vfs::Status status)
{
    serial::puts(key);
    serial::puts("=");
    serial::puts(vfs::status_name(status));
    serial::puts("(");
    serial::put_hex8(static_cast<uint8_t>(status));
    serial::puts(")\n");
}

static void log_folder_text(const char* key, const char* value)
{
    serial::puts(key);
    serial::puts("=");
    serial::puts(value && value[0] ? value : "(empty)");
    serial::puts("\n");
}

PasteResult move_to_trash(const char* sourcePath, char* outTrashedPath, size_t outTrashedPathSize)
{
    trace_marker("TRASH_MOVE_BEGIN");
    trace_text("TRASH_SOURCE_PATH", sourcePath);
    reset_diagnostic(PasteResult::Failed);
    if (s_operationActive) {
        s_lastDiagnostic.result = PasteResult::Busy;
        return PasteResult::Busy;
    }
    if (!sourcePath || !sourcePath[0] || !outTrashedPath || outTrashedPathSize == 0) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        return PasteResult::Failed;
    }

    outTrashedPath[0] = '\0';
    char normalizedSource[vfs::VFS_MAX_PATH] = {0};
    vfs::normalize_path(sourcePath, normalizedSource, sizeof(normalizedSource));
    copy_diagnostic_path(s_lastDiagnostic.sourcePath, sizeof(s_lastDiagnostic.sourcePath), normalizedSource);
    set_diagnostic_stage(PasteStage::SourceValidation);

    vfs::FileInfo sourceInfo{};
    vfs::Status sourceStatus = vfs::stat(normalizedSource, &sourceInfo);
    trace_text("TRASH_SOURCE_PATH", normalizedSource);
    trace_status("TRASH_SOURCE_STAT", sourceStatus);
    if (sourceStatus != vfs::VFS_OK) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::SourceMissing,
                               sourceStatus, fat_status_for_vfs_status(sourceStatus));
        return PasteResult::SourceMissing;
    }
    if (sourceInfo.type != vfs::FILE_TYPE_REGULAR && sourceInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_NOT_SUPPORTED, "FAT_FILE_WRITE_UNSUPPORTED_TYPE");
        return PasteResult::Unsupported;
    }
    trace_text("TRASH_SOURCE_TYPE",
               sourceInfo.type == vfs::FILE_TYPE_REGULAR ? "REGULAR_FILE" : "DIRECTORY");
    if (same_path(normalizedSource, "/") || same_path(normalizedSource, kTrashRootPath)) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_PROTECTED_PATH");
        return PasteResult::Unsupported;
    }

    const char* sourceName = vfs::basename(normalizedSource);
    if (!sourceName || !sourceName[0]) {
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::Failed,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_NAME");
        return PasteResult::Failed;
    }

    // Keep the operation state active through all VFS mutation and prevent
    // mouse/key dispatch from re-entering paste or Delete on bare metal.
    s_operationActive = true;
    fs_fat::set_trash_trace(true);
    s_progress = FileOperationProgress{};
    s_progress.operation = Operation::Move;
    s_progress.state = OperationState::Preparing;
    s_progress.phase = PasteStage::SourceValidation;
    copy_text(s_progress.sourceDisplayName, sizeof(s_progress.sourceDisplayName), sourceName);
    s_progress.totalKnown = sourceInfo.type == vfs::FILE_TYPE_REGULAR;
    s_progress.totalBytes = s_progress.totalKnown ? sourceInfo.size : 0;
    notify_progress();

    char trashRoot[vfs::VFS_MAX_PATH] = {0};
    if (!trash_root_for_path(normalizedSource, trashRoot, sizeof(trashRoot))) {
        trace_text("TRASH_ROOT_RESOLVE", "VFS_ERR_NOT_MOUNT");
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::DestinationMissing,
                               vfs::VFS_ERR_NOT_MOUNT, "FAT_FILE_WRITE_NOT_MOUNTED");
        return finish_operation(PasteResult::DestinationMissing);
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationDirectory,
                         sizeof(s_lastDiagnostic.destinationDirectory), trashRoot);
    copy_diagnostic_path(s_progress.destinationPath, sizeof(s_progress.destinationPath), trashRoot);
    trace_text("TRASH_ROOT_PATH", trashRoot);
    trace_text("TRASH_ROOT_RESOLVE", "BEGIN");
    set_diagnostic_stage(PasteStage::DestinationValidation);

    vfs::FileInfo trashInfo{};
    vfs::Status trashStatus = vfs::stat(trashRoot, &trashInfo);
    if (trashStatus == vfs::VFS_ERR_NOT_FOUND) {
        trashStatus = vfs::mkdir(trashRoot);
        if (trashStatus == vfs::VFS_OK) trashStatus = vfs::stat(trashRoot, &trashInfo);
    }
    if (trashStatus != vfs::VFS_OK || trashInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        if (trashStatus == vfs::VFS_OK) trashStatus = vfs::VFS_ERR_NOT_DIR;
        trace_status("TRASH_ROOT_RESOLVE", trashStatus);
        const PasteResult rootResult = trashStatus == vfs::VFS_ERR_READ_ONLY
            ? PasteResult::ReadOnly : PasteResult::DestinationMissing;
        set_diagnostic_failure(PasteStage::DestinationValidation, rootResult,
                               trashStatus, fat_status_for_vfs_status(trashStatus));
        return finish_operation(rootResult);
    }
    trace_status("TRASH_ROOT_RESOLVE", vfs::VFS_OK);
    const vfs::MountPoint* sourceMount = mount_for_path(normalizedSource);
    const vfs::MountPoint* trashMount = mount_for_path(trashRoot);
    const uint8_t sourceMountId = vfs::mount_index_for_path(normalizedSource);
    const uint8_t trashMountId = vfs::mount_index_for_path(trashRoot);
    serial::puts("TRASH_SOURCE_MOUNT_ID=0x");
    serial::put_hex8(sourceMountId);
    serial::puts(" TRASH_ROOT_MOUNT_ID=0x");
    serial::put_hex8(trashMountId);
    serial::puts("\n");
    trace_text("TRASH_SAME_MOUNT", sourceMount && trashMount && sourceMount == trashMount ? "1" : "0");
    if (!sourceMount || !trashMount || sourceMount != trashMount) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_NOT_SUPPORTED, "FAT_FILE_WRITE_UNSUPPORTED_TYPE");
        return finish_operation(PasteResult::Unsupported);
    }
    if (sourceMount->readOnly || trashMount->readOnly) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::ReadOnly,
                               vfs::VFS_ERR_READ_ONLY, "FAT_FILE_WRITE_READ_ONLY");
        return finish_operation(PasteResult::ReadOnly);
    }

    set_diagnostic_stage(PasteStage::DestinationNaming);
    char movedPath[vfs::VFS_MAX_PATH] = {0};
    if (!choose_trash_path(trashRoot, sourceName,
                           sourceInfo.type == vfs::FILE_TYPE_DIRECTORY,
                           movedPath, sizeof(movedPath))) {
        set_diagnostic_failure(PasteStage::DestinationNaming, PasteResult::Conflict,
                               vfs::VFS_ERR_EXISTS, "FAT_FILE_WRITE_ALREADY_EXISTS");
        return finish_operation(PasteResult::Conflict);
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationPath,
                         sizeof(s_lastDiagnostic.destinationPath), movedPath);
    copy_diagnostic_path(s_progress.destinationPath, sizeof(s_progress.destinationPath), movedPath);
    trace_text("TRASH_DEST_NAME", vfs::basename(movedPath));
    trace_text("TRASH_DEST_PATH", movedPath);
    char metadataPath[vfs::VFS_MAX_PATH] = {0};
    if (trash_metadata_path_for(movedPath, metadataPath, sizeof(metadataPath))) {
        trace_text("TRASH_METADATA_PATH", metadataPath);
    }

    // Re-stat directly before the first mutation. The copy/delete transaction
    // leaves the source untouched until destination verification and metadata
    // creation have both succeeded.
    vfs::FileInfo revalidated{};
    sourceStatus = vfs::stat(normalizedSource, &revalidated);
    if (sourceStatus != vfs::VFS_OK || revalidated.type != sourceInfo.type) {
        const PasteResult result = sourceStatus == vfs::VFS_ERR_READ_ONLY
            ? PasteResult::ReadOnly : PasteResult::SourceMissing;
        set_diagnostic_failure(PasteStage::SourceValidation, result,
                               sourceStatus == vfs::VFS_OK ? vfs::VFS_ERR_INVALID : sourceStatus,
                               fat_status_for_vfs_status(sourceStatus));
        return finish_operation(result);
    }

    // FAT rename_path currently cannot complete a cross-directory directory
    // entry write reliably on the supported bare-metal ATA path. Use the
    // verified copy/delete transaction until that lower-layer primitive is
    // complete; it preserves the source until the destination and metadata
    // have both been flushed and verified.
    trace_text("TRASH_STRATEGY", "copy-delete");
    trace_text("TRASH_RENAME_UNAVAILABLE", "FAT_CROSS_DIRECTORY_ENTRY_WRITE");
    trace_marker("TRASH_DEST_CREATE_BEGIN");
    trace_text("TRASH_DEST_CREATE_KIND", "copy");
    set_operation_state(OperationState::Moving);
    set_diagnostic_stage(PasteStage::DestinationCreate);
    const PasteResult copyResult = copy_entry_tree(normalizedSource, movedPath,
                                                   sourceInfo, 0);
    if (copyResult != PasteResult::Success) {
        vfs::Status copyStatus = s_lastDiagnostic.vfsStatus;
        if (copyStatus == vfs::VFS_OK) copyStatus = vfs::VFS_ERR_IO;
        const char* copyFatStatus = s_lastDiagnostic.fatStatus[0]
            ? s_lastDiagnostic.fatStatus : fat_status_for_vfs_status(copyStatus);
        const PasteResult cleanupResult = remove_entry_tree(movedPath, 0);
        trace_status("TRASH_DEST_CREATE_RESULT", copyStatus);
        trace_status("TRASH_VERIFY", cleanupResult == PasteResult::Success
            ? vfs::VFS_OK : vfs::VFS_ERR_IO);
        set_diagnostic_failure(PasteStage::DestinationCreate, PasteResult::Failed,
                               copyStatus, copyFatStatus);
        return finish_operation(PasteResult::Failed);
    }
    trace_status("TRASH_DEST_CREATE_RESULT", vfs::VFS_OK);

    set_operation_state(OperationState::Verifying);
    set_diagnostic_stage(PasteStage::Verification);
    if (!verify_entry_tree(normalizedSource, movedPath, sourceInfo, 0)) {
        const PasteResult cleanupResult = remove_entry_tree(movedPath, 0);
        trace_text("TRASH_VERIFY", "FAILED");
        trace_status("TRASH_VERIFY_ROLLBACK", cleanupResult == PasteResult::Success
            ? vfs::VFS_OK : vfs::VFS_ERR_IO);
        set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_VERIFY_FAILED");
        return finish_operation(PasteResult::Failed);
    }
    trace_text("TRASH_VERIFY", "OK");

    vfs::Status metadataStatus = vfs::VFS_OK;
    if (!write_trash_metadata(movedPath, normalizedSource,
                              sourceInfo.type == vfs::FILE_TYPE_DIRECTORY,
                              &metadataStatus)) {
        remove_trash_metadata(movedPath);
        const PasteResult cleanupResult = remove_entry_tree(movedPath, 0);
        trace_status("TRASH_VERIFY_ROLLBACK", cleanupResult == PasteResult::Success
            ? vfs::VFS_OK : vfs::VFS_ERR_IO);
        set_diagnostic_failure(PasteStage::DestinationCreate, PasteResult::Failed,
                               metadataStatus, fat_status_for_vfs_status(metadataStatus));
        return finish_operation(PasteResult::Failed);
    }

    set_diagnostic_stage(PasteStage::Flush);
    trace_text("TRASH_SOURCE_DELETE", "BEGIN");
    const PasteResult sourceDeleteResult = remove_entry_tree(normalizedSource, 0);
    if (sourceDeleteResult != PasteResult::Success) {
        remove_trash_metadata(movedPath);
        const PasteResult cleanupResult = remove_entry_tree(movedPath, 0);
        trace_status("TRASH_SOURCE_DELETE", vfs::VFS_ERR_IO);
        trace_status("TRASH_VERIFY_ROLLBACK", cleanupResult == PasteResult::Success
            ? vfs::VFS_OK : vfs::VFS_ERR_IO);
        set_diagnostic_failure(PasteStage::Flush, PasteResult::Failed,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_SOURCE_DELETE_FAILED");
        return finish_operation(PasteResult::Failed);
    }
    trace_status("TRASH_SOURCE_DELETE", vfs::VFS_OK);
    copy_diagnostic_path(outTrashedPath, outTrashedPathSize, movedPath);
    set_diagnostic_stage(PasteStage::Complete);
    s_lastDiagnostic.result = PasteResult::Success;
    const bool clearClipboard = has_pending_file() && same_path(s_sourcePath, normalizedSource);
    PasteResult result = finish_operation(PasteResult::Success);
    if (clearClipboard) clear();
    trace_marker("TRASH_MOVE_COMPLETE");
    return result;
}

bool create_unique_folder(const char* destinationDirectory, char* outPath, size_t outPathSize)
{
    return create_unique_folder_ex(destinationDirectory, outPath, outPathSize, nullptr);
}

bool create_unique_folder_ex(const char* destinationDirectory,
                             char* outPath,
                             size_t outPathSize,
                             vfs::Status* outStatus)
{
    vfs::Status finalStatus = vfs::VFS_ERR_INVALID;
    if (outStatus) *outStatus = finalStatus;
    if (outPath && outPathSize > 0) outPath[0] = '\0';

    serial::puts("DESKTOP_MKDIR_BEGIN\n");
    log_folder_text("DESKTOP_MKDIR_PARENT", destinationDirectory);
    if (!destinationDirectory || !destinationDirectory[0] || !outPath || outPathSize == 0) {
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=argument-validation\n");
        return false;
    }

    char normalizedDestination[vfs::VFS_MAX_PATH];
    vfs::normalize_path(destinationDirectory, normalizedDestination, sizeof(normalizedDestination));
    log_folder_text("DESKTOP_MKDIR_PARENT_NORMALIZED", normalizedDestination);

    vfs::FileInfo parentInfo{};
    const vfs::Status parentStatus = vfs::stat(normalizedDestination, &parentInfo);
    serial::puts("DESKTOP_MKDIR_PARENT_EXISTS=");
    serial::puts(parentStatus == vfs::VFS_OK && parentInfo.type == vfs::FILE_TYPE_DIRECTORY ? "1" : "0");
    serial::puts("\n");
    log_folder_status("DESKTOP_MKDIR_PARENT_STATUS", parentStatus);
    if (parentStatus != vfs::VFS_OK) {
        finalStatus = parentStatus;
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=parent-resolution\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }
    if (parentInfo.type != vfs::FILE_TYPE_DIRECTORY) {
        finalStatus = vfs::VFS_ERR_NOT_DIR;
        log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=parent-type\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }

    const vfs::MountPoint* mount = vfs::get_mount(normalizedDestination);
    const uint8_t mountIndex = vfs::mount_index_for_path(normalizedDestination);
    if (!mount) {
        finalStatus = vfs::VFS_ERR_NOT_MOUNT;
        log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=mount-resolution\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }
    serial::puts("DESKTOP_MKDIR_FS=");
    serial::puts(vfs::fs_type_name(mount->fsType));
    serial::puts(" mountId=0x");
    serial::put_hex8(mountIndex);
    serial::puts(" mount=");
    serial::puts(mount->path);
    serial::puts(" device=0x");
    serial::put_hex8(mount->blockDevIndex);
    serial::puts(" writable=");
    serial::puts(mount->readOnly ? "0" : "1");
    serial::puts(" alias=");
    serial::puts(mount->alias ? "1" : "0");
    serial::puts(" source=");
    serial::puts(mount->sourcePrefix[0] ? mount->sourcePrefix : "/");
    serial::puts("\n");
    if (mount->readOnly) {
        finalStatus = vfs::VFS_ERR_READ_ONLY;
        log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=writable-check\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }
    if (mount->fsType != vfs::FS_TYPE_FAT32) {
        finalStatus = vfs::VFS_ERR_NOT_SUPPORTED;
        log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=filesystem-type\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }

    char folderName[vfs::VFS_MAX_FILENAME];
    for (int suffixIndex = 1; suffixIndex < kMaxNameCandidates; ++suffixIndex) {
        if (!make_new_folder_name(suffixIndex, folderName, sizeof(folderName))) {
            finalStatus = vfs::VFS_ERR_INVALID;
            log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
            serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=name-generation\n");
            if (outStatus) *outStatus = finalStatus;
            return false;
        }
        log_folder_text("DESKTOP_MKDIR_NAME", folderName);
        char candidatePath[vfs::VFS_MAX_PATH];
        vfs::join_path(normalizedDestination, folderName, candidatePath, sizeof(candidatePath));
        log_folder_text("DESKTOP_MKDIR_PATH", candidatePath);
        if (!candidatePath[0]) {
            finalStatus = vfs::VFS_ERR_INVALID;
            log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
            serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=path-join\n");
            if (outStatus) *outStatus = finalStatus;
            return false;
        }

        vfs::FileInfo existing{};
        const vfs::Status candidateStatus = vfs::stat(candidatePath, &existing);
        log_folder_status("DESKTOP_MKDIR_CANDIDATE_STAT", candidateStatus);
        if (candidateStatus == vfs::VFS_OK) {
            serial::puts("DESKTOP_MKDIR_COLLISION=1\n");
            continue;
        }
        if (candidateStatus != vfs::VFS_ERR_NOT_FOUND) {
            finalStatus = candidateStatus;
            log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
            serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=candidate-resolution\n");
            if (outStatus) *outStatus = finalStatus;
            return false;
        }

        serial::puts("DESKTOP_MKDIR_VFS=kernel::vfs::mkdir\n");
        finalStatus = vfs::mkdir(candidatePath);
        log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
        if (finalStatus == vfs::VFS_OK) {
            vfs::FileInfo createdInfo{};
            const vfs::Status verifyStatus = vfs::stat(candidatePath, &createdInfo);
            serial::puts("DESKTOP_MKDIR_VERIFY=");
            serial::puts(verifyStatus == vfs::VFS_OK &&
                         createdInfo.type == vfs::FILE_TYPE_DIRECTORY ? "1" : "0");
            serial::puts("\n");
            log_folder_status("DESKTOP_MKDIR_VERIFY_STATUS", verifyStatus);
            if (verifyStatus != vfs::VFS_OK || createdInfo.type != vfs::FILE_TYPE_DIRECTORY) {
                finalStatus = verifyStatus == vfs::VFS_OK ? vfs::VFS_ERR_NOT_DIR : verifyStatus;
                serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=verification\n");
                if (outStatus) *outStatus = finalStatus;
                return false;
            }
            if (!copy_text(outPath, outPathSize, candidatePath)) {
                finalStatus = vfs::VFS_ERR_INVALID;
                log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
                serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=result-copy\n");
                if (outStatus) *outStatus = finalStatus;
                return false;
            }
            ++s_operationGeneration;
            serial::puts("DESKTOP_MKDIR_SUCCESS=1\n");
            if (outStatus) *outStatus = vfs::VFS_OK;
            return true;
        }
        if (finalStatus == vfs::VFS_ERR_EXISTS) {
            serial::puts("DESKTOP_MKDIR_COLLISION=1\n");
            continue;
        }
        serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=creation\n");
        if (outStatus) *outStatus = finalStatus;
        return false;
    }

    finalStatus = vfs::VFS_ERR_EXISTS;
    log_folder_status("DESKTOP_MKDIR_RESULT", finalStatus);
    serial::puts("DESKTOP_MKDIR_FAILURE_STAGE=name-exhaustion\n");
    if (outStatus) *outStatus = finalStatus;
    return false;
}

const char* paste_result_message(PasteResult result) {
    if (result == PasteResult::Success) return "Pasted item";
    if (result == PasteResult::Busy) return "A file operation is already in progress";
    return paste_diagnostic_message();
}

const char* paste_stage_name(PasteStage stage) {
    return paste_stage_name_local(stage);
}

const PasteDiagnostic& last_paste_diagnostic() {
    return s_lastDiagnostic;
}

const char* paste_diagnostic_message() {
    s_diagnosticMessage[0] = '\0';
    if (s_lastDiagnostic.result == PasteResult::Success) return "Pasted item";
    if (s_lastDiagnostic.result == PasteResult::Empty) return "Clipboard is empty";
    if (s_lastDiagnostic.result == PasteResult::Busy) return "A file operation is already in progress";

    append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), "File operation failed: ");
    if (s_lastDiagnostic.vfsStatus != vfs::VFS_OK) {
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage),
                    vfs::status_name(s_lastDiagnostic.vfsStatus));
    } else if (s_lastDiagnostic.fatStatus[0]) {
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), s_lastDiagnostic.fatStatus);
    } else {
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage),
                    paste_result_name_local(s_lastDiagnostic.result));
    }
    append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), " at ");
    append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage),
                paste_stage_name_local(s_lastDiagnostic.stage));
    if (s_lastDiagnostic.fatStatus[0] && s_lastDiagnostic.vfsStatus != vfs::VFS_OK) {
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), " (");
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), s_lastDiagnostic.fatStatus);
        append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), ")");
    }
    return s_diagnosticMessage;
}

bool begin_paste_refresh() {
    if (s_operationActive || s_lastDiagnostic.result != PasteResult::Success) return false;
    s_operationActive = true;
    s_progress.state = OperationState::Refreshing;
    s_progress.phase = PasteStage::Refresh;
    notify_progress();
    return true;
}

void note_paste_refresh(bool success) {
    if (s_lastDiagnostic.result != PasteResult::Success) return;
    set_diagnostic_stage(PasteStage::Refresh);
    trace_text("DESKTOP_PASTE_REFRESH", success ? "SUCCESS" : "FAILED");
    if (!success) {
        s_lastDiagnostic.result = PasteResult::Failed;
        s_lastDiagnostic.vfsStatus = vfs::VFS_ERR_IO;
        copy_text(s_lastDiagnostic.fatStatus, sizeof(s_lastDiagnostic.fatStatus),
                  "FAT_FILE_WRITE_IO_ERROR");
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Failed));
        trace_marker("FPASTE_COMPLETE_FAILED");
        s_progress.result = PasteResult::Failed;
        s_progress.state = OperationState::Failed;
        s_progress.phase = PasteStage::Refresh;
        notify_progress();
        s_operationActive = false;
        return;
    }
    set_diagnostic_stage(PasteStage::Complete);
    trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
    trace_marker("FPASTE_CLIPBOARD_FINALIZE");
    trace_marker("FPASTE_COMPLETE");
    s_progress.result = PasteResult::Success;
    s_progress.state = OperationState::Completed;
    s_progress.phase = PasteStage::Complete;
    notify_progress();
    s_operationActive = false;
}

#if defined(GXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
static bool smoke_write_file(const char* path, const uint8_t* bytes, size_t byteCount) {
    return path && (bytes || byteCount == 0) && vfs::write_file(path, bytes, static_cast<uint32_t>(byteCount)) ==
        static_cast<int32_t>(byteCount);
}

static bool smoke_text_equals(const char* left, const char* right) {
    if (!left || !right) return false;
    size_t index = 0;
    while (left[index] || right[index]) {
        if (left[index] != right[index]) return false;
        ++index;
    }
    return true;
}

static bool smoke_write_pattern_file(const char* path, size_t byteCount) {
    if (!path || byteCount > 0xFFFFFFFFull) return false;
    const uint8_t handle = vfs::open(path,
        vfs::OPEN_WRITE | vfs::OPEN_CREATE | vfs::OPEN_EXCL);
    if (handle == 0xFF) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < byteCount) {
        const uint32_t request = static_cast<uint32_t>(
            byteCount - offset > kCopyBufferBytes ? kCopyBufferBytes : byteCount - offset);
        for (uint32_t index = 0; index < request; ++index) {
            s_copyBuffer[index] = static_cast<uint8_t>(((offset + index) * 37u + 0x5Au) & 0xFFu);
        }
        const int32_t written = vfs::write(handle, s_copyBuffer, request);
        if (written != static_cast<int32_t>(request)) {
            ok = false;
            break;
        }
        offset += request;
    }
    if (ok) ok = vfs::flush(handle) == vfs::VFS_OK;
    if (vfs::close(handle) != vfs::VFS_OK) ok = false;
    if (!ok) vfs::unlink(path);
    return ok && offset == byteCount;
}

static bool smoke_file_matches_pattern(const char* path, size_t expectedSize) {
    if (!path) return false;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size != expectedSize) return false;

    const uint8_t handle = vfs::open(path, vfs::OPEN_READ);
    if (handle == 0xFF) return false;
    uint8_t bytes[4096];
    size_t offset = 0;
    bool ok = true;
    while (offset < expectedSize) {
        const uint32_t request = static_cast<uint32_t>(
            expectedSize - offset > sizeof(bytes) ? sizeof(bytes) : expectedSize - offset);
        const int32_t count = vfs::read(handle, bytes, request);
        if (count != static_cast<int32_t>(request)) {
            serial::puts("[FILE-OPS-RUNTIME-SMOKE] pattern-read-mismatch path=");
            serial::puts(path);
            serial::puts(" offset=");
            serial::put_hex64(offset);
            serial::puts(" requested=");
            serial::put_hex32(request);
            serial::puts(" actual=");
            serial::put_hex32(count < 0 ? 0 : static_cast<uint32_t>(count));
            serial::puts("\n");
            ok = false;
            break;
        }
        for (uint32_t index = 0; index < request; ++index) {
            const uint8_t expected = static_cast<uint8_t>(((offset + index) * 37u + 0x5Au) & 0xFFu);
            if (bytes[index] != expected) {
                serial::puts("[FILE-OPS-RUNTIME-SMOKE] pattern-mismatch path=");
                serial::puts(path);
                serial::puts(" offset=");
                serial::put_hex64(offset + index);
                serial::puts(" expected=0x");
                serial::put_hex8(expected);
                serial::puts(" actual=0x");
                serial::put_hex8(bytes[index]);
                serial::puts("\n");
                ok = false;
                break;
            }
        }
        if (!ok) break;
        offset += request;
    }
    if (vfs::close(handle) != vfs::VFS_OK) ok = false;
    return ok && offset == expectedSize;
}

static bool smoke_file_equals(const char* path, const uint8_t* expected, size_t expectedSize) {
    if (!path || (!expected && expectedSize != 0)) return false;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size != expectedSize) return false;
    if (expectedSize == 0) return true;
    uint8_t bytes[32] = {0};
    if (expectedSize > sizeof(bytes)) return false;
    const int32_t read = vfs::read_file(path, bytes, static_cast<uint32_t>(expectedSize));
    if (read != static_cast<int32_t>(expectedSize)) return false;
    for (size_t i = 0; i < expectedSize; ++i) {
        if (bytes[i] != expected[i]) return false;
    }
    return true;
}

static bool smoke_is_directory(const char* path) {
    vfs::FileInfo info{};
    return path && vfs::stat(path, &info) == vfs::VFS_OK && info.type == vfs::FILE_TYPE_DIRECTORY;
}

static void smoke_phase(const char* phase) {
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] phase=");
    serial::puts(phase ? phase : "unknown");
    serial::puts("\n");
}

static bool smoke_check(const char* label, bool value) {
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] check=");
    serial::puts(label ? label : "unknown");
    serial::puts(" result=");
    serial::puts(value ? "PASS\n" : "FAIL\n");
    return value;
}

static bool smoke_direct_trash_create_probe(const char* trashRoot) {
    if (!trashRoot || !trashRoot[0]) return false;
    char probePath[vfs::VFS_MAX_PATH];
    vfs::join_path(trashRoot, "TEST.TXT", probePath, sizeof(probePath));
    if (!probePath[0]) return false;
    vfs::unlink(probePath);

    static const uint8_t probeBytes[] = {'O', 'K', '\n'};
    const int32_t created = vfs::create_file(probePath, probeBytes, sizeof(probeBytes));
    if (created != static_cast<int32_t>(sizeof(probeBytes))) return false;
    const uint8_t handle = vfs::open(probePath, vfs::OPEN_READ);
    if (handle == 0xFF) {
        vfs::unlink(probePath);
        return false;
    }
    uint8_t readBytes[sizeof(probeBytes)] = {0};
    const int32_t read = vfs::read(handle, readBytes, sizeof(readBytes));
    const bool closed = vfs::close(handle) == vfs::VFS_OK;
    bool bytesMatch = read == static_cast<int32_t>(sizeof(probeBytes));
    for (size_t i = 0; bytesMatch && i < sizeof(probeBytes); ++i) {
        bytesMatch = readBytes[i] == probeBytes[i];
    }
    const vfs::Status removed = vfs::unlink(probePath);
    return closed && bytesMatch && removed == vfs::VFS_OK && !vfs::exists(probePath);
}

void run_runtime_smoke() {
    static const uint8_t sourceBytes[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    static const uint8_t existingBytes[] = {0xCA, 0xFE};
    static const char* root = "/Desktop/GXSMK5";
    static const char* destination = "/Desktop/GXSMK5/OPDST";
    static const char* folder = "/Desktop/GXSMK5/OPFOLD";
    static const char* nested = "/Desktop/GXSMK5/OPFOLD/SUBDIR";
    bool ok = true;

    serial::puts("[FILE-OPS-RUNTIME-SMOKE] start\n");
    clear();
    remove_entry_tree(root, 0);

    const vfs::MountPoint* desktopMount = vfs::get_mount("/Desktop");
    if (!desktopMount || desktopMount->readOnly || desktopMount->fsType != vfs::FS_TYPE_FAT32) {
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] unavailable=desktop-writable-fat32\n");
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] result=FAIL\n");
        return;
    }

    ok &= vfs::mkdir(root) == vfs::VFS_OK;
    ok &= vfs::mkdir(destination) == vfs::VFS_OK;
    ok &= vfs::mkdir(folder) == vfs::VFS_OK;
    ok &= vfs::mkdir(nested) == vfs::VFS_OK;
    char emptyDirectory[ vfs::VFS_MAX_PATH ];
    vfs::join_path(folder, "EMPTYDIR", emptyDirectory, sizeof(emptyDirectory));
    ok &= vfs::mkdir(emptyDirectory) == vfs::VFS_OK;

    // Exercise the exact FAT short-name boundary independently of the
    // desktop naming policy. This catches a filesystem-layer rejection of
    // otherwise valid names before a collision-safe name is blamed.
    static const char* shortNames[] = {"TEST", "FOLDER", "NEWFOLD", "NEWFOL01"};
    for (size_t shortIndex = 0; shortIndex < sizeof(shortNames) / sizeof(shortNames[0]); ++shortIndex) {
        char shortPath[vfs::VFS_MAX_PATH];
        vfs::join_path(root, shortNames[shortIndex], shortPath, sizeof(shortPath));
        const vfs::Status shortStatus = vfs::mkdir(shortPath);
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] shortName=");
        serial::puts(shortNames[shortIndex]);
        serial::puts(" status=");
        serial::puts(vfs::status_name(shortStatus));
        serial::puts("\n");
        ok &= shortStatus == vfs::VFS_OK;
        ok &= smoke_is_directory(shortPath);
        ok &= vfs::mkdir(shortPath) == vfs::VFS_ERR_EXISTS;
    }
    ok &= vfs::mkdir("/Desktop/GXSMK5/MISSING/TEST") == vfs::VFS_ERR_NOT_FOUND;

    const vfs::Status readOnlyStatus = vfs::mkdir("/system/TEST");
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] readOnlyMount status=");
    serial::puts(vfs::status_name(readOnlyStatus));
    serial::puts("\n");
    ok &= readOnlyStatus == vfs::VFS_ERR_READ_ONLY;

    char sourceFile[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "SRC.TXT", sourceFile, sizeof(sourceFile));
    ok &= smoke_write_file(sourceFile, sourceBytes, sizeof(sourceBytes));

    // Trash uses the same mounted FAT tree as the source. Probe the exact
    // destination API before Move-to-Trash so metadata/path failures cannot be
    // confused with data transfer failures.
    char trashRoot[vfs::VFS_MAX_PATH];
    ok &= trash_root_for_path(sourceFile, trashRoot, sizeof(trashRoot));
    if (ok && !smoke_is_directory(trashRoot)) {
        ok &= vfs::mkdir(trashRoot) == vfs::VFS_OK;
    }
    ok &= smoke_check("trash-root-resolves-after-mount", smoke_is_directory(trashRoot));
    ok &= smoke_check("trash-direct-create", smoke_direct_trash_create_probe(trashRoot));

    char trashSource[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "src.txt", trashSource, sizeof(trashSource));
    ok &= smoke_write_file(trashSource, sourceBytes, sizeof(sourceBytes));
    char firstTrashedPath[vfs::VFS_MAX_PATH] = {0};
    const PasteResult firstTrashResult = move_to_trash(
        trashSource, firstTrashedPath, sizeof(firstTrashedPath));
    char firstMetadataPath[vfs::VFS_MAX_PATH] = {0};
    ok &= smoke_check("trash-small-file-move", firstTrashResult == PasteResult::Success);
    ok &= smoke_check("trash-source-preserved-until-success", !vfs::exists(trashSource));
    ok &= smoke_check("trash-small-file-bytes",
                      firstTrashResult == PasteResult::Success &&
                      smoke_file_equals(firstTrashedPath, sourceBytes, sizeof(sourceBytes)));
    ok &= smoke_check("trash-metadata-fat-name",
                      trash_metadata_path_for(firstTrashedPath, firstMetadataPath,
                                              sizeof(firstMetadataPath)) &&
                      is_trash_metadata_name(vfs::basename(firstMetadataPath)) &&
                      vfs::exists(firstMetadataPath));

    ok &= smoke_write_file(trashSource, sourceBytes, sizeof(sourceBytes));
    char secondTrashedPath[vfs::VFS_MAX_PATH] = {0};
    const PasteResult secondTrashResult = move_to_trash(
        trashSource, secondTrashedPath, sizeof(secondTrashedPath));
    ok &= smoke_check("trash-collision-unique",
                      secondTrashResult == PasteResult::Success &&
                      !same_path(firstTrashedPath, secondTrashedPath));
    char secondMetadataPath[vfs::VFS_MAX_PATH] = {0};
    trash_metadata_path_for(secondTrashedPath, secondMetadataPath, sizeof(secondMetadataPath));

    char trashEmptyFolder[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "EMPTYTRS", trashEmptyFolder, sizeof(trashEmptyFolder));
    ok &= vfs::mkdir(trashEmptyFolder) == vfs::VFS_OK;
    char movedEmptyFolder[vfs::VFS_MAX_PATH] = {0};
    ok &= move_to_trash(trashEmptyFolder, movedEmptyFolder, sizeof(movedEmptyFolder)) == PasteResult::Success;
    ok &= smoke_check("trash-empty-folder", !vfs::exists(trashEmptyFolder) && smoke_is_directory(movedEmptyFolder));
    char emptyMetadataPath[vfs::VFS_MAX_PATH] = {0};
    trash_metadata_path_for(movedEmptyFolder, emptyMetadataPath, sizeof(emptyMetadataPath));

    char trashTree[vfs::VFS_MAX_PATH];
    char trashTreeNested[vfs::VFS_MAX_PATH];
    char trashTreeFile[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "TREETRS", trashTree, sizeof(trashTree));
    vfs::join_path(trashTree, "NEST", trashTreeNested, sizeof(trashTreeNested));
    vfs::join_path(trashTreeNested, "CHILD.TXT", trashTreeFile, sizeof(trashTreeFile));
    ok &= vfs::mkdir(trashTree) == vfs::VFS_OK;
    ok &= vfs::mkdir(trashTreeNested) == vfs::VFS_OK;
    ok &= smoke_write_file(trashTreeFile, sourceBytes, sizeof(sourceBytes));
    char movedTree[vfs::VFS_MAX_PATH] = {0};
    ok &= move_to_trash(trashTree, movedTree, sizeof(movedTree)) == PasteResult::Success;
    char movedTreeFile[vfs::VFS_MAX_PATH];
    vfs::join_path(movedTree, "NEST/CHILD.TXT", movedTreeFile, sizeof(movedTreeFile));
    ok &= smoke_check("trash-nonempty-folder",
                      !vfs::exists(trashTree) && smoke_file_equals(movedTreeFile,
                                                                     sourceBytes, sizeof(sourceBytes)));
    char treeMetadataPath[vfs::VFS_MAX_PATH] = {0};
    trash_metadata_path_for(movedTree, treeMetadataPath, sizeof(treeMetadataPath));

    char testPng[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "TEST.PNG", testPng, sizeof(testPng));
    ok &= smoke_write_file(testPng, sourceBytes, sizeof(sourceBytes));

    char desktopRootPng[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "BMROOT.PNG", desktopRootPng, sizeof(desktopRootPng));
    ok &= smoke_write_file(desktopRootPng, sourceBytes, sizeof(sourceBytes));

    char imagePng[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "IMAGE.PNG", imagePng, sizeof(imagePng));
    static const size_t multiClusterImageSize = 131123;
    ok &= smoke_write_pattern_file(imagePng, multiClusterImageSize);

    char zeroPng[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "ZERO.PNG", zeroPng, sizeof(zeroPng));
    ok &= smoke_write_file(zeroPng, nullptr, 0);

    char imageCollisionSource[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "IMAGE001.PNG", imageCollisionSource, sizeof(imageCollisionSource));
    ok &= smoke_write_file(imageCollisionSource, sourceBytes, sizeof(sourceBytes));
    char imageCollisionExisting[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "IMAGE001.PNG", imageCollisionExisting, sizeof(imageCollisionExisting));
    ok &= smoke_write_file(imageCollisionExisting, existingBytes, sizeof(existingBytes));

    char generatedName[vfs::VFS_MAX_FILENAME];
    ok &= smoke_check("candidate-space", make_candidate_name("my picture.png", 0,
                                                               generatedName, sizeof(generatedName)) &&
                      smoke_text_equals(generatedName, "MYPICTUR.PNG"));
    ok &= smoke_check("candidate-long", make_candidate_name("long-desktop-image-name.png", 0,
                                                               generatedName, sizeof(generatedName)) &&
                      smoke_text_equals(generatedName, "LONG-DES.PNG"));
    ok &= smoke_check("candidate-collision", make_candidate_name("IMAGE001.PNG", 1,
                                                                    generatedName, sizeof(generatedName)) &&
                      smoke_text_equals(generatedName, "IMAGE0~1.PNG"));

    ok &= set_file(testPng, Operation::Copy);
    const PasteResult readOnlyPaste = paste_to_directory("/system/wall");
    ok &= smoke_check("read-only-destination", readOnlyPaste == PasteResult::ReadOnly);
    ok &= smoke_file_equals(testPng, sourceBytes, sizeof(sourceBytes));
    clear();

    char nestedFile[vfs::VFS_MAX_PATH];
    vfs::join_path(nested, "NEST.TXT", nestedFile, sizeof(nestedFile));
    ok &= smoke_write_file(nestedFile, sourceBytes, sizeof(sourceBytes));
    smoke_phase("files-written");

    const uint64_t generationBefore = operation_generation();
    ok &= set_file(sourceFile, Operation::Copy);
    smoke_phase("copy-file-begin");
    ok &= paste_to_directory(destination) == PasteResult::Success;
    smoke_phase("copy-file-complete");
    char copiedFile[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "SRC.TXT", copiedFile, sizeof(copiedFile));
    ok &= smoke_file_equals(sourceFile, sourceBytes, sizeof(sourceBytes));
    smoke_phase("source-verified");
    ok &= smoke_file_equals(copiedFile, sourceBytes, sizeof(sourceBytes));
    smoke_phase("copy-verified");

    ok &= set_file(testPng, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char copiedTestPng[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "TEST.PNG", copiedTestPng, sizeof(copiedTestPng));
    ok &= smoke_file_equals(testPng, sourceBytes, sizeof(sourceBytes));
    ok &= smoke_file_equals(copiedTestPng, sourceBytes, sizeof(sourceBytes));

    // Exercise the same root Desktop destination used by the background
    // Paste command. The physical desktop may resolve this path through its
    // post-mount alias; the VFS call remains the shared operation boundary.
    ok &= set_file(desktopRootPng, Operation::Copy);
    ok &= paste_to_directory("/Desktop") == PasteResult::Success;
    char copiedDesktopRootPng[vfs::VFS_MAX_PATH];
    vfs::join_path("/Desktop", "BMROOT.PNG", copiedDesktopRootPng, sizeof(copiedDesktopRootPng));
    ok &= smoke_file_equals(desktopRootPng, sourceBytes, sizeof(sourceBytes));
    ok &= smoke_file_equals(copiedDesktopRootPng, sourceBytes, sizeof(sourceBytes));
    ok &= vfs::unlink(copiedDesktopRootPng) == vfs::VFS_OK;
    smoke_phase(ok ? "after-desktop-root-pass" : "after-desktop-root-fail");

    ok &= set_file(imagePng, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char copiedImagePng[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "IMAGE.PNG", copiedImagePng, sizeof(copiedImagePng));
    ok &= smoke_check("multi-source-pattern",
                      smoke_file_matches_pattern(imagePng, multiClusterImageSize));
    ok &= smoke_check("multi-destination-pattern",
                      smoke_file_matches_pattern(copiedImagePng, multiClusterImageSize));
    smoke_phase(ok ? "after-multicluster-png-pass" : "after-multicluster-png-fail");

    // Cut the same multi-cluster source after the copy case. The destination
    // already contains IMAGE.PNG, so this also proves collision naming and
    // source deletion ordering for a multi-cluster move.
    ok &= set_file(imagePng, Operation::Move);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char movedImagePng[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "IMAGE~1.PNG", movedImagePng, sizeof(movedImagePng));
    ok &= !vfs::exists(imagePng);
    ok &= smoke_check("multicluster-cut-destination",
                      smoke_file_matches_pattern(movedImagePng, multiClusterImageSize));
    smoke_phase(ok ? "after-multicluster-cut-pass" : "after-multicluster-cut-fail");

    ok &= set_file(zeroPng, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char copiedZeroPng[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "ZERO.PNG", copiedZeroPng, sizeof(copiedZeroPng));
    ok &= smoke_check("zero-source", smoke_file_equals(zeroPng, nullptr, 0));
    ok &= smoke_check("zero-destination", smoke_file_equals(copiedZeroPng, nullptr, 0));

    ok &= set_file(imageCollisionSource, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char imageCollisionCopy[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "IMAGE0~1.PNG", imageCollisionCopy, sizeof(imageCollisionCopy));
    ok &= smoke_file_equals(imageCollisionExisting, existingBytes, sizeof(existingBytes));
    ok &= smoke_file_equals(imageCollisionCopy, sourceBytes, sizeof(sourceBytes));
    ok &= smoke_file_equals(imageCollisionSource, sourceBytes, sizeof(sourceBytes));
    smoke_phase(ok ? "after-png-cases-pass" : "after-png-cases-fail");

    ok &= set_file(sourceFile, Operation::Move);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char movedFile[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "SRC~1.TXT", movedFile, sizeof(movedFile));
    ok &= !vfs::exists(sourceFile);
    ok &= smoke_file_equals(movedFile, sourceBytes, sizeof(sourceBytes));
    smoke_phase(ok ? "after-move-pass" : "after-move-fail");

    char collisionSource[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "COLLIDE.TXT", collisionSource, sizeof(collisionSource));
    ok &= smoke_write_file(collisionSource, sourceBytes, sizeof(sourceBytes));
    char collisionExisting[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "COLLIDE.TXT", collisionExisting, sizeof(collisionExisting));
    ok &= smoke_write_file(collisionExisting, existingBytes, sizeof(existingBytes));
    ok &= set_file(collisionSource, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char collisionCopy[vfs::VFS_MAX_PATH];
    // The shared candidate generator reserves two characters for ~N, so the
    // FAT-valid 8.3 result is COLLID~1.TXT (not the invalid nine-character
    // COLLIDE~1.TXT spelling).
    vfs::join_path(destination, "COLLID~1.TXT", collisionCopy, sizeof(collisionCopy));
    ok &= smoke_file_equals(collisionExisting, existingBytes, sizeof(existingBytes));
    ok &= smoke_file_equals(collisionCopy, sourceBytes, sizeof(sourceBytes));
    smoke_phase(ok ? "after-collision-pass" : "after-collision-fail");

    ok &= set_file(folder, Operation::Copy);
    ok &= paste_to_directory(destination) == PasteResult::Success;
    char copiedFolder[vfs::VFS_MAX_PATH];
    vfs::join_path(destination, "OPFOLD", copiedFolder, sizeof(copiedFolder));
    char copiedNested[vfs::VFS_MAX_PATH];
    vfs::join_path(copiedFolder, "SUBDIR/NEST.TXT", copiedNested, sizeof(copiedNested));
    char copiedEmpty[vfs::VFS_MAX_PATH];
    vfs::join_path(copiedFolder, "EMPTYDIR", copiedEmpty, sizeof(copiedEmpty));
    ok &= smoke_is_directory(copiedFolder) && smoke_file_equals(copiedNested, sourceBytes, sizeof(sourceBytes));
    ok &= smoke_is_directory(copiedEmpty);
    ok &= set_file(folder, Operation::Copy);
    ok &= paste_to_directory(nested) == PasteResult::Unsupported;
    ok &= smoke_is_directory(folder) && smoke_file_equals(nestedFile, sourceBytes, sizeof(sourceBytes));
    ok &= set_file(folder, Operation::Move);
    ok &= paste_to_directory(nested) == PasteResult::Unsupported;
    ok &= smoke_is_directory(folder) && has_pending_file();
    clear();
    smoke_phase(ok ? "after-folder-pass" : "after-folder-fail");

    char newFolderOne[vfs::VFS_MAX_PATH];
    char newFolderTwo[vfs::VFS_MAX_PATH];
    ok &= create_unique_folder(destination, newFolderOne, sizeof(newFolderOne));
    ok &= create_unique_folder(destination, newFolderTwo, sizeof(newFolderTwo));
    ok &= newFolderOne[0] && newFolderTwo[0] && !same_path(newFolderOne, newFolderTwo);
    ok &= smoke_is_directory(newFolderOne) && smoke_is_directory(newFolderTwo);
    ok &= operation_generation() > generationBefore;
    smoke_phase(ok ? "after-new-folders-pass" : "after-new-folders-fail");

    // Deterministic large-file threshold suite. Each source is generated with
    // the same byte pattern, copied through the production clipboard path,
    // and checked at both source and destination. The names remain FAT 8.3
    // compatible so a failure cannot be attributed to long-name handling.
    static const char* thresholdRoot = "/Desktop/GXSMK6";
    static const char* thresholdDestination = "/Desktop/GXSMK6/DST";
    static const char* thresholdNames[] = {
        "F0000000.BIN", "F0000001.BIN", "F0004096.BIN", "F0065536.BIN",
        "F0262144.BIN", "F0524288.BIN", "F1048576.BIN", "F1572864.BIN",
        "F2097152.BIN", "F3145728.BIN"
    };
    static const size_t thresholdSizes[] = {
        0u, 1u, 4096u, 65536u, 262144u, 524288u,
        1048576u, 1572864u, 2097152u, 3145728u
    };
    remove_entry_tree(thresholdRoot, 0);
    ok &= vfs::mkdir(thresholdRoot) == vfs::VFS_OK;
    ok &= vfs::mkdir(thresholdDestination) == vfs::VFS_OK;
    const fs_fat::FATVolume* desktopVolume =
        fs_fat::get_volume(desktopMount->fsVolumeIndex);
    if (desktopVolume) {
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] threshold clusterBytes=");
        serial::put_hex32(desktopVolume->bytesPerSector * desktopVolume->sectorsPerCluster);
        serial::puts("\n");
    }
    for (size_t thresholdIndex = 0;
         thresholdIndex < sizeof(thresholdSizes) / sizeof(thresholdSizes[0]) && ok;
         ++thresholdIndex) {
        char thresholdSource[vfs::VFS_MAX_PATH];
        char thresholdCopy[vfs::VFS_MAX_PATH];
        vfs::join_path(thresholdRoot, thresholdNames[thresholdIndex],
                       thresholdSource, sizeof(thresholdSource));
        vfs::join_path(thresholdDestination, thresholdNames[thresholdIndex],
                       thresholdCopy, sizeof(thresholdCopy));
        const bool generated = smoke_write_pattern_file(
            thresholdSource, thresholdSizes[thresholdIndex]);
        const bool copied = generated && set_file(thresholdSource, Operation::Copy) &&
                            paste_to_directory(thresholdDestination) == PasteResult::Success;
        const bool sourceOk = copied && smoke_file_matches_pattern(
            thresholdSource, thresholdSizes[thresholdIndex]);
        const bool destinationOk = copied && smoke_file_matches_pattern(
            thresholdCopy, thresholdSizes[thresholdIndex]);
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] threshold name=");
        serial::puts(thresholdNames[thresholdIndex]);
        serial::puts(" size=");
        serial::put_hex64(thresholdSizes[thresholdIndex]);
        serial::puts(" result=");
        serial::puts(generated && copied && sourceOk && destinationOk ? "PASS\n" : "FAIL\n");
        ok &= generated && copied && sourceOk && destinationOk;
    }

    // The failing-size collision and Desktop-root cases use the same 2 MiB
    // deterministic source, while the normal threshold loop targets a folder.
    if (ok) {
        char largeSource[vfs::VFS_MAX_PATH];
        char largeCollision[vfs::VFS_MAX_PATH];
        vfs::join_path(thresholdRoot, "F2097152.BIN", largeSource, sizeof(largeSource));
        ok &= set_file(largeSource, Operation::Copy);
        ok &= paste_to_directory(thresholdDestination) == PasteResult::Success;
        char collisionName[vfs::VFS_MAX_FILENAME];
        ok &= make_candidate_name("F2097152.BIN", 1,
                                  collisionName, sizeof(collisionName));
        vfs::join_path(thresholdDestination, collisionName,
                       largeCollision, sizeof(largeCollision));
        ok &= smoke_file_matches_pattern(largeSource, 2097152u);
        ok &= smoke_file_matches_pattern(largeCollision, 2097152u);
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] threshold-2MiB-collision result=");
        serial::puts(ok ? "PASS\n" : "FAIL\n");

        char rootCopy[vfs::VFS_MAX_PATH];
        vfs::join_path("/Desktop", "F2097152.BIN", rootCopy, sizeof(rootCopy));
        vfs::unlink(rootCopy);
        ok &= set_file(largeSource, Operation::Copy);
        ok &= paste_to_directory("/Desktop") == PasteResult::Success;
        ok &= smoke_file_matches_pattern(rootCopy, 2097152u);
        ok &= vfs::unlink(rootCopy) == vfs::VFS_OK;
        serial::puts("[FILE-OPS-RUNTIME-SMOKE] threshold-2MiB-desktop-root result=");
        serial::puts(ok ? "PASS\n" : "FAIL\n");
    }

    char staleFile[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "STALE.TXT", staleFile, sizeof(staleFile));
    ok &= smoke_write_file(staleFile, sourceBytes, sizeof(sourceBytes));
    ok &= set_file(staleFile, Operation::Move);
    ok &= vfs::unlink(staleFile) == vfs::VFS_OK;
    ok &= paste_to_directory(destination) == PasteResult::SourceMissing;
    ok &= !has_pending_file();
    smoke_phase(ok ? "before-cleanup-pass" : "before-cleanup-fail");

    clear();
    if (firstMetadataPath[0]) vfs::unlink(firstMetadataPath);
    if (secondMetadataPath[0]) vfs::unlink(secondMetadataPath);
    if (emptyMetadataPath[0]) vfs::unlink(emptyMetadataPath);
    if (treeMetadataPath[0]) vfs::unlink(treeMetadataPath);
    remove_entry_tree(firstTrashedPath, 0);
    remove_entry_tree(secondTrashedPath, 0);
    remove_entry_tree(movedEmptyFolder, 0);
    remove_entry_tree(movedTree, 0);
    remove_entry_tree(thresholdRoot, 0);
    const PasteResult cleanupResult = remove_entry_tree(root, 0);
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] cleanup=");
    serial::puts(paste_result_message(cleanupResult));
    serial::puts("\n");
    ok &= cleanupResult == PasteResult::Success;
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] result=");
    serial::puts(ok ? "PASS\n" : "FAIL\n");
}

void run_trash_runtime_smoke() {
    static const uint8_t sourceBytes[] = {'t', 'r', 'a', 's', 'h', '\n'};
    static const char* sourcePath = "/desktop/GXOPSMK/src.txt";
    static const char* readOnlySource = "/system/wall/ivsmoke.png";
    bool ok = true;

    serial::puts("[FILE-OPS-TRASH-SMOKE] start\n");
    clear();

    const vfs::MountPoint* sourceMount = vfs::get_mount(sourcePath);
    ok &= smoke_check("source-mount", sourceMount != nullptr);
    if (!sourceMount || sourceMount->readOnly || sourceMount->fsType != vfs::FS_TYPE_FAT32) {
        serial::puts("[FILE-OPS-TRASH-SMOKE] result=FAIL\n");
        return;
    }

    char trashRoot[vfs::VFS_MAX_PATH] = {0};
    ok &= trash_root_for_path(sourcePath, trashRoot, sizeof(trashRoot));
    if (ok && !smoke_is_directory(trashRoot)) {
        ok &= vfs::mkdir(trashRoot) == vfs::VFS_OK;
    }
    ok &= smoke_check("trash-root-resolves-after-mount", smoke_is_directory(trashRoot));
    ok &= smoke_check("trash-direct-create", smoke_direct_trash_create_probe(trashRoot));

    ok &= smoke_write_file(sourcePath, sourceBytes, sizeof(sourceBytes));
    char firstTrashedPath[vfs::VFS_MAX_PATH] = {0};
    const PasteResult firstResult = move_to_trash(
        sourcePath, firstTrashedPath, sizeof(firstTrashedPath));
    char firstMetadataPath[vfs::VFS_MAX_PATH] = {0};
    const bool firstMetadataName =
        trash_metadata_path_for(firstTrashedPath, firstMetadataPath, sizeof(firstMetadataPath)) &&
        is_trash_metadata_name(vfs::basename(firstMetadataPath));
    ok &= smoke_check("small-file-move", firstResult == PasteResult::Success);
    ok &= smoke_check("source-removed-after-success", !vfs::exists(sourcePath));
    ok &= smoke_check("destination-bytes", firstResult == PasteResult::Success &&
                      smoke_file_equals(firstTrashedPath, sourceBytes, sizeof(sourceBytes)));
    ok &= smoke_check("metadata-fat-name", firstMetadataName && vfs::exists(firstMetadataPath));

    ok &= smoke_write_file(sourcePath, sourceBytes, sizeof(sourceBytes));
    char secondTrashedPath[vfs::VFS_MAX_PATH] = {0};
    const PasteResult secondResult = move_to_trash(
        sourcePath, secondTrashedPath, sizeof(secondTrashedPath));
    char secondMetadataPath[vfs::VFS_MAX_PATH] = {0};
    const bool secondMetadata =
        trash_metadata_path_for(secondTrashedPath, secondMetadataPath, sizeof(secondMetadataPath));
    ok &= smoke_check("collision-unique", secondResult == PasteResult::Success &&
                      !same_path(firstTrashedPath, secondTrashedPath));
    ok &= smoke_check("collision-bytes", secondResult == PasteResult::Success &&
                      smoke_file_equals(secondTrashedPath, sourceBytes, sizeof(sourceBytes)));
    ok &= smoke_check("collision-metadata", secondMetadata && vfs::exists(secondMetadataPath));

    ok &= set_file(readOnlySource, Operation::Copy);
    char ignoredTrashedPath[vfs::VFS_MAX_PATH] = {0};
    const PasteResult readOnlyResult = move_to_trash(
        readOnlySource, ignoredTrashedPath, sizeof(ignoredTrashedPath));
    ok &= smoke_check("failure-result-preserves-source", readOnlyResult == PasteResult::ReadOnly &&
                      vfs::exists(readOnlySource));
    ok &= smoke_check("failure-preserves-clipboard", has_pending_file());
    clear();

    if (firstMetadataPath[0]) vfs::unlink(firstMetadataPath);
    if (secondMetadataPath[0]) vfs::unlink(secondMetadataPath);
    if (firstTrashedPath[0]) vfs::unlink(firstTrashedPath);
    if (secondTrashedPath[0]) vfs::unlink(secondTrashedPath);
    ok &= smoke_check("trash-cleanup", !vfs::exists(firstTrashedPath) &&
                      !vfs::exists(secondTrashedPath));

    serial::puts("[FILE-OPS-TRASH-SMOKE] result=");
    serial::puts(ok ? "PASS\n" : "FAIL\n");
}
#endif

} // namespace file_clipboard
} // namespace kernel
