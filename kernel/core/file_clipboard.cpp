//
// guideXOS bare-metal file clipboard implementation
//

#include "include/kernel/file_clipboard.h"
#include "include/kernel/serial_debug.h"
#include "include/kernel/vfs.h"

namespace kernel {
namespace file_clipboard {

static const uint32_t kMaxCopyBytes = 8u * 1024u * 1024u;
static const int kMaxNameCandidates = 1000;

// File operations are deliberately bounded. The buffer is in BSS and is
// shared by the synchronous paste path, so there is no unbounded allocation.
static uint8_t s_copyBuffer[kMaxCopyBytes];
static char s_sourcePath[vfs::VFS_MAX_PATH] = {0};
static char s_sourceName[vfs::VFS_MAX_FILENAME] = {0};
static Operation s_operation = Operation::None;
static uint64_t s_operationGeneration = 0;
static PasteDiagnostic s_lastDiagnostic{};
static char s_diagnosticMessage[192] = {0};

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

static void reset_diagnostic(PasteResult result) {
    s_lastDiagnostic = PasteDiagnostic{};
    s_lastDiagnostic.result = result;
}

static void set_diagnostic_stage(PasteStage stage) {
    s_lastDiagnostic.stage = stage;
    trace_text("DESKTOP_PASTE_STAGE", paste_stage_name_local(stage));
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
    return c > 32 && c < 127 && c != '/' && c != '\\';
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

static bool same_path(const char* left, const char* right) {
    if (!left || !right) return false;
    char normalizedLeft[vfs::VFS_MAX_PATH];
    char normalizedRight[vfs::VFS_MAX_PATH];
    vfs::normalize_path(left, normalizedLeft, sizeof(normalizedLeft));
    vfs::normalize_path(right, normalizedRight, sizeof(normalizedRight));
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
    vfs::normalize_path(path, normalizedPath, sizeof(normalizedPath));
    vfs::normalize_path(ancestor, normalizedAncestor, sizeof(normalizedAncestor));
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
    if (sourceSize > kMaxCopyBytes) {
        set_diagnostic_failure(PasteStage::DataTransfer, PasteResult::Unsupported,
                               vfs::VFS_ERR_NO_SPACE, "FAT_FILE_WRITE_NO_SPACE");
        return PasteResult::Unsupported;
    }

    const uint32_t byteCount = static_cast<uint32_t>(sourceSize);
    set_diagnostic_stage(PasteStage::SourceOpenRead);
    trace_text("DESKTOP_PASTE_SOURCE_OPEN", sourcePath);
    const uint8_t sourceHandle = vfs::open(sourcePath, vfs::OPEN_READ);
    if (sourceHandle == 0xFF) {
        set_diagnostic_failure(PasteStage::SourceOpenRead, PasteResult::SourceMissing,
                               vfs::VFS_ERR_NOT_FOUND, "FAT_FILE_WRITE_NOT_FOUND");
        return PasteResult::SourceMissing;
    }
    trace_text("DESKTOP_PASTE_SOURCE_OPEN_STATUS", "VFS_OK");
    const int32_t bytesRead = byteCount == 0
        ? 0 : vfs::read(sourceHandle, s_copyBuffer, byteCount);
    s_lastDiagnostic.bytesRead = bytesRead > 0 ? static_cast<uint64_t>(bytesRead) : 0;
    trace_u64("DESKTOP_PASTE_BYTES_READ", s_lastDiagnostic.bytesRead);
    const vfs::Status sourceCloseStatus = vfs::close(sourceHandle);
    trace_status("DESKTOP_PASTE_SOURCE_CLOSE", sourceCloseStatus);
    if (sourceCloseStatus != vfs::VFS_OK) {
        set_diagnostic_failure(PasteStage::SourceOpenRead, PasteResult::Failed,
                               sourceCloseStatus, "FAT_FILE_WRITE_IO_ERROR");
        return PasteResult::Failed;
    }
    if (bytesRead < 0) {
        set_diagnostic_failure(PasteStage::SourceOpenRead, PasteResult::Failed,
                               static_cast<vfs::Status>(bytesRead), "FAT_FILE_WRITE_IO_ERROR");
        return PasteResult::Failed;
    }
    if (bytesRead != static_cast<int32_t>(byteCount)) {
        set_diagnostic_failure(PasteStage::DataTransfer, PasteResult::Failed,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
        return PasteResult::Failed;
    }

    set_diagnostic_stage(PasteStage::DataTransfer);
    trace_text("DESKTOP_PASTE_DATA_COPY", "VFS_READ_TO_EXCLUSIVE_CREATE");
    set_diagnostic_stage(PasteStage::DestinationCreate);
    trace_text("DESKTOP_PASTE_DEST_CREATE", destinationPath);
    const int32_t bytesWritten = vfs::create_file(
        destinationPath, byteCount == 0 ? nullptr : s_copyBuffer, byteCount);
    s_lastDiagnostic.bytesWritten = bytesWritten > 0 ? static_cast<uint64_t>(bytesWritten) : 0;
    trace_u64("DESKTOP_PASTE_BYTES_WRITTEN", s_lastDiagnostic.bytesWritten);
    if (bytesWritten != static_cast<int32_t>(byteCount)) {
        const vfs::Status writeStatus = bytesWritten < 0
            ? static_cast<vfs::Status>(bytesWritten) : vfs::VFS_ERR_IO;
        trace_status("DESKTOP_PASTE_DEST_CREATE_STATUS", writeStatus);
        const vfs::Status cleanupStatus = vfs::unlink(destinationPath);
        trace_status("DESKTOP_PASTE_ROLLBACK", cleanupStatus);
        set_diagnostic_failure(PasteStage::DestinationCreate, PasteResult::Failed,
                               writeStatus, fat_status_for_vfs_status(writeStatus));
        return PasteResult::Failed;
    }
    trace_status("DESKTOP_PASTE_DEST_CREATE_STATUS", vfs::VFS_OK);

    // FAT writes publish their data, FAT chain, and directory metadata
    // synchronously. Keep an explicit trace point so the operation contract
    // remains visible even though there is no buffered file handle to flush.
    set_diagnostic_stage(PasteStage::Flush);
    trace_status("DESKTOP_PASTE_FLUSH", vfs::VFS_OK);

    set_diagnostic_stage(PasteStage::Verification);
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
    trace_text("DESKTOP_PASTE_VERIFY", "VFS_OK size-match");
    return PasteResult::Success;
}

static PasteResult remove_entry_tree(const char* path) {
    if (!path || !path[0]) return PasteResult::Failed;

    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK) return PasteResult::Success;
    if (info.type == vfs::FILE_TYPE_REGULAR) {
        return vfs::unlink(path) == vfs::VFS_OK ? PasteResult::Success : PasteResult::Failed;
    }
    if (info.type != vfs::FILE_TYPE_DIRECTORY) return PasteResult::Failed;

    for (;;) {
        vfs::DirEntry child{};
        if (!read_directory_entry_at(path, 0, &child)) break;
        char childPath[vfs::VFS_MAX_PATH];
        vfs::join_path(path, child.name, childPath, sizeof(childPath));
        if (!childPath[0] || remove_entry_tree(childPath) != PasteResult::Success) {
            return PasteResult::Failed;
        }
    }
    return vfs::rmdir(path) == vfs::VFS_OK ? PasteResult::Success : PasteResult::Failed;
}

static PasteResult copy_entry_tree(const char* sourcePath, const char* destinationPath,
                                   const vfs::FileInfo& sourceInfo) {
    if (!sourcePath || !destinationPath) return PasteResult::Failed;
    if (sourceInfo.type == vfs::FILE_TYPE_REGULAR) {
        return copy_file_contents(sourcePath, destinationPath, sourceInfo.size);
    }
    if (sourceInfo.type != vfs::FILE_TYPE_DIRECTORY) return PasteResult::Unsupported;

    if (vfs::mkdir(destinationPath) != vfs::VFS_OK) return PasteResult::Failed;
    for (size_t childIndex = 0;; ++childIndex) {
        vfs::DirEntry child{};
        if (!read_directory_entry_at(sourcePath, childIndex, &child)) break;

        char childSourcePath[vfs::VFS_MAX_PATH];
        char childDestinationPath[vfs::VFS_MAX_PATH];
        vfs::join_path(sourcePath, child.name, childSourcePath, sizeof(childSourcePath));
        vfs::join_path(destinationPath, child.name, childDestinationPath, sizeof(childDestinationPath));
        if (!childSourcePath[0] || !childDestinationPath[0]) {
            remove_entry_tree(destinationPath);
            return PasteResult::Failed;
        }

        vfs::FileInfo childInfo{};
        if (vfs::stat(childSourcePath, &childInfo) != vfs::VFS_OK) {
            remove_entry_tree(destinationPath);
            return PasteResult::SourceMissing;
        }
        PasteResult childResult = copy_entry_tree(childSourcePath, childDestinationPath, childInfo);
        if (childResult != PasteResult::Success) {
            remove_entry_tree(destinationPath);
            return childResult;
        }
    }
    return PasteResult::Success;
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

bool set_file(const char* sourcePath, Operation operation) {
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
    trace_text("FILE_CLIPBOARD_COPY_SET", operation == Operation::Copy ? "COPY" : "MOVE");
    trace_text("FILE_CLIPBOARD_SOURCE_TYPE", info.type == vfs::FILE_TYPE_REGULAR ? "REGULAR_FILE" : "DIRECTORY");
    trace_text("FILE_CLIPBOARD_SOURCE_NAME", s_sourceName);
    return true;
}

void clear() {
    s_sourcePath[0] = '\0';
    s_sourceName[0] = '\0';
    s_operation = Operation::None;
}

bool has_pending_file() {
    return s_operation != Operation::None && s_sourcePath[0] && s_sourceName[0];
}

Operation pending_operation() {
    return s_operation;
}

uint64_t operation_generation() {
    return s_operationGeneration;
}

bool can_paste_to(const char* destinationDirectory) {
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
    reset_diagnostic(PasteResult::Failed);
    serial::puts("DESKTOP_PASTE_BEGIN\n");
    if (!has_pending_file()) {
        set_diagnostic_stage(PasteStage::SourceValidation);
        s_lastDiagnostic.result = PasteResult::Empty;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Empty));
        return PasteResult::Empty;
    }

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
    if (sourceStatus != vfs::VFS_OK ||
        (sourceInfo.type != vfs::FILE_TYPE_REGULAR && sourceInfo.type != vfs::FILE_TYPE_DIRECTORY)) {
        clear();
        set_diagnostic_failure(PasteStage::SourceValidation, PasteResult::SourceMissing,
                               sourceStatus == vfs::VFS_OK ? vfs::VFS_ERR_INVALID : sourceStatus,
                               "FAT_FILE_WRITE_NOT_FOUND");
        return PasteResult::SourceMissing;
    }

    char normalizedDestination[vfs::VFS_MAX_PATH] = {0};
    if (destinationDirectory && destinationDirectory[0]) {
        vfs::normalize_path(destinationDirectory, normalizedDestination, sizeof(normalizedDestination));
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationDirectory,
                         sizeof(s_lastDiagnostic.destinationDirectory), normalizedDestination);
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
        return result;
    }

    const vfs::MountPoint* destinationMount = vfs::get_mount(normalizedDestination);
    if (!destinationMount) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::DestinationMissing,
                               vfs::VFS_ERR_NOT_MOUNT, "FAT_FILE_WRITE_NOT_MOUNTED");
        return PasteResult::DestinationMissing;
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
        return PasteResult::ReadOnly;
    }
    if (destinationMount->fsType != vfs::FS_TYPE_FAT32) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_NOT_SUPPORTED, "FAT_FILE_WRITE_UNSUPPORTED_TYPE");
        return PasteResult::Unsupported;
    }

    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY &&
        same_or_descendant_path(normalizedDestination, s_sourcePath)) {
        set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::Unsupported,
                               vfs::VFS_ERR_INVALID, "FAT_FILE_WRITE_INVALID_ARGUMENT");
        return PasteResult::Unsupported;
    }

    set_diagnostic_stage(PasteStage::DestinationNaming);
    char destinationPath[vfs::VFS_MAX_PATH];
    if (!choose_destination_path(normalizedDestination, destinationPath, sizeof(destinationPath))) {
        set_diagnostic_failure(PasteStage::DestinationNaming, PasteResult::Conflict,
                               vfs::VFS_ERR_EXISTS, "FAT_FILE_WRITE_ALREADY_EXISTS");
        return PasteResult::Conflict;
    }
    copy_diagnostic_path(s_lastDiagnostic.destinationPath,
                         sizeof(s_lastDiagnostic.destinationPath), destinationPath);
    trace_text("DESKTOP_PASTE_DEST_NAME", vfs::basename(destinationPath));
    trace_text("DESKTOP_PASTE_DEST_PATH", destinationPath);

    // Cutting an item into its current directory is a safe no-op. Copying to
    // the same directory deliberately takes the deterministic Copy name path.
    char sourceDestinationPath[vfs::VFS_MAX_PATH];
    vfs::join_path(normalizedDestination, s_sourceName,
                   sourceDestinationPath, sizeof(sourceDestinationPath));
    if (s_operation == Operation::Move && same_path(s_sourcePath, sourceDestinationPath)) {
        clear();
        ++s_operationGeneration;
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
        return PasteResult::Success;
    }

    if (s_operation == Operation::Move) {
        const vfs::MountPoint* sourceMount = vfs::get_mount(s_sourcePath);
        destinationMount = vfs::get_mount(destinationPath);
        if (!sourceMount || !destinationMount) {
            set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::DestinationMissing,
                                   vfs::VFS_ERR_NOT_MOUNT, "FAT_FILE_WRITE_NOT_MOUNTED");
            return PasteResult::DestinationMissing;
        }
        if (sourceMount->readOnly) {
            set_diagnostic_failure(PasteStage::DestinationValidation, PasteResult::ReadOnly,
                                   vfs::VFS_ERR_READ_ONLY, "FAT_FILE_WRITE_READ_ONLY");
            return PasteResult::ReadOnly;
        }

        if (sourceMount == destinationMount) {
            set_diagnostic_stage(PasteStage::DataTransfer);
            trace_text("DESKTOP_PASTE_MOVE_RENAME", "VFS_RENAME");
            const vfs::Status renameStatus = vfs::rename(s_sourcePath, destinationPath);
            trace_status("DESKTOP_PASTE_MOVE_RENAME_STATUS", renameStatus);
            if (renameStatus == vfs::VFS_OK) {
                set_diagnostic_stage(PasteStage::Flush);
                trace_status("DESKTOP_PASTE_FLUSH", vfs::VFS_OK);
                vfs::FileInfo movedInfo{};
                const vfs::Status verifyStatus = vfs::stat(destinationPath, &movedInfo);
                trace_status("DESKTOP_PASTE_VERIFY_STATUS", verifyStatus);
                if (verifyStatus == vfs::VFS_OK && movedInfo.type == sourceInfo.type &&
                    (sourceInfo.type != vfs::FILE_TYPE_REGULAR || movedInfo.size == sourceInfo.size)) {
                    clear();
                    ++s_operationGeneration;
                    set_diagnostic_stage(PasteStage::Complete);
                    s_lastDiagnostic.result = PasteResult::Success;
                    trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
                    return PasteResult::Success;
                }
                vfs::rename(destinationPath, s_sourcePath);
                set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                                       verifyStatus == vfs::VFS_OK ? vfs::VFS_ERR_IO : verifyStatus,
                                       fat_status_for_vfs_status(verifyStatus));
                return PasteResult::Failed;
            }
        }

        // Cross-filesystem moves, and filesystems without atomic rename, use
        // copy-then-delete. The source is never deleted until the destination
        // tree has been created successfully.
        PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo);
        if (copied != PasteResult::Success) return copied;
        PasteResult removed = remove_entry_tree(s_sourcePath);
        if (removed != PasteResult::Success) {
            remove_entry_tree(destinationPath);
            set_diagnostic_failure(PasteStage::Verification, PasteResult::Failed,
                                   vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
            return PasteResult::Failed;
        }
        clear();
        ++s_operationGeneration;
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
        return PasteResult::Success;
    }

    PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo);
    if (copied == PasteResult::Success) {
        ++s_operationGeneration;
        set_diagnostic_stage(PasteStage::Complete);
        s_lastDiagnostic.result = PasteResult::Success;
        trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
    } else if (s_lastDiagnostic.stage == PasteStage::None) {
        set_diagnostic_failure(PasteStage::DataTransfer, copied,
                               vfs::VFS_ERR_IO, "FAT_FILE_WRITE_IO_ERROR");
    }
    return copied;
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

    append_text(s_diagnosticMessage, sizeof(s_diagnosticMessage), "Paste Failed: ");
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
        return;
    }
    set_diagnostic_stage(PasteStage::Complete);
    trace_text("DESKTOP_PASTE_FINAL_RESULT", paste_result_name_local(PasteResult::Success));
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
    if (!path || byteCount > kMaxCopyBytes) return false;
    for (size_t index = 0; index < byteCount; ++index) {
        s_copyBuffer[index] = static_cast<uint8_t>((index * 37u + 0x5Au) & 0xFFu);
    }
    return smoke_write_file(path, s_copyBuffer, byteCount);
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
    remove_entry_tree(root);

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

    char staleFile[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "STALE.TXT", staleFile, sizeof(staleFile));
    ok &= smoke_write_file(staleFile, sourceBytes, sizeof(sourceBytes));
    ok &= set_file(staleFile, Operation::Move);
    ok &= vfs::unlink(staleFile) == vfs::VFS_OK;
    ok &= paste_to_directory(destination) == PasteResult::SourceMissing;
    ok &= !has_pending_file();
    smoke_phase(ok ? "before-cleanup-pass" : "before-cleanup-fail");

    clear();
    const PasteResult cleanupResult = remove_entry_tree(root);
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] cleanup=");
    serial::puts(paste_result_message(cleanupResult));
    serial::puts("\n");
    ok &= cleanupResult == PasteResult::Success;
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] result=");
    serial::puts(ok ? "PASS\n" : "FAIL\n");
}
#endif

} // namespace file_clipboard
} // namespace kernel
