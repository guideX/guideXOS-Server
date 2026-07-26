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
    if (!sourcePath || !destinationPath || sourceSize > kMaxCopyBytes) return PasteResult::Unsupported;

    const uint32_t byteCount = static_cast<uint32_t>(sourceSize);
    int32_t bytesRead = 0;
    if (byteCount > 0) {
        bytesRead = vfs::read_file(sourcePath, s_copyBuffer, byteCount);
        if (bytesRead != static_cast<int32_t>(byteCount)) return PasteResult::Failed;
    }

    int32_t bytesWritten = vfs::write_file(destinationPath, s_copyBuffer, byteCount);
    if (bytesWritten != static_cast<int32_t>(byteCount)) {
        vfs::unlink(destinationPath);
        return PasteResult::Failed;
    }

    vfs::FileInfo destinationInfo{};
    if (vfs::stat(destinationPath, &destinationInfo) != vfs::VFS_OK ||
        destinationInfo.type != vfs::FILE_TYPE_REGULAR || destinationInfo.size != sourceSize) {
        vfs::unlink(destinationPath);
        return PasteResult::Failed;
    }
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
    if (!sourcePath || !sourcePath[0] || operation == Operation::None) return false;

    char normalizedPath[vfs::VFS_MAX_PATH];
    vfs::normalize_path(sourcePath, normalizedPath, sizeof(normalizedPath));
    vfs::FileInfo info{};
    if (!normalizedPath[0] || vfs::stat(normalizedPath, &info) != vfs::VFS_OK ||
        (info.type != vfs::FILE_TYPE_REGULAR && info.type != vfs::FILE_TYPE_DIRECTORY)) {
        return false;
    }

    const char* name = vfs::basename(normalizedPath);
    if (!name || !name[0]) return false;
    char nextSourcePath[vfs::VFS_MAX_PATH];
    char nextSourceName[vfs::VFS_MAX_FILENAME];
    if (!copy_text(nextSourcePath, sizeof(nextSourcePath), normalizedPath) ||
        !copy_text(nextSourceName, sizeof(nextSourceName), name)) {
        return false;
    }

    copy_text(s_sourcePath, sizeof(s_sourcePath), nextSourcePath);
    copy_text(s_sourceName, sizeof(s_sourceName), nextSourceName);
    s_operation = operation;
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
    if (!has_pending_file()) return PasteResult::Empty;

    vfs::FileInfo sourceInfo{};
    if (!source_is_valid(&sourceInfo)) {
        clear();
        return PasteResult::SourceMissing;
    }

    char normalizedDestination[vfs::VFS_MAX_PATH];
    if (!destination_is_valid(destinationDirectory, normalizedDestination, sizeof(normalizedDestination))) {
        vfs::FileInfo destinationInfo{};
        if (!destinationDirectory || vfs::stat(destinationDirectory, &destinationInfo) != vfs::VFS_OK ||
            destinationInfo.type != vfs::FILE_TYPE_DIRECTORY) {
            return PasteResult::DestinationMissing;
        }
        const vfs::MountPoint* destinationMount = vfs::get_mount(normalizedDestination);
        if (!destinationMount) return PasteResult::DestinationMissing;
        if (destinationMount->readOnly) return PasteResult::ReadOnly;
        return PasteResult::Unsupported;
    }

    if (sourceInfo.type == vfs::FILE_TYPE_DIRECTORY &&
        same_or_descendant_path(normalizedDestination, s_sourcePath)) {
        return PasteResult::Unsupported;
    }

    char sourceDestinationPath[vfs::VFS_MAX_PATH];
    vfs::join_path(normalizedDestination, s_sourceName, sourceDestinationPath, sizeof(sourceDestinationPath));
    if (!sourceDestinationPath[0]) return PasteResult::Unsupported;

    // Cutting an item into its current directory is a safe no-op. Copying to
    // the same directory deliberately takes the deterministic Copy name path.
    if (s_operation == Operation::Move && same_path(s_sourcePath, sourceDestinationPath)) {
        clear();
        ++s_operationGeneration;
        return PasteResult::Success;
    }

    char destinationPath[vfs::VFS_MAX_PATH];
    if (!choose_destination_path(normalizedDestination, destinationPath, sizeof(destinationPath))) {
        return PasteResult::Conflict;
    }

    if (s_operation == Operation::Move) {
        const vfs::MountPoint* sourceMount = vfs::get_mount(s_sourcePath);
        const vfs::MountPoint* destinationMount = vfs::get_mount(destinationPath);
        if (!sourceMount || !destinationMount) return PasteResult::DestinationMissing;
        if (sourceMount->readOnly) return PasteResult::ReadOnly;

        if (sourceMount == destinationMount) {
            vfs::Status renameStatus = vfs::rename(s_sourcePath, destinationPath);
            if (renameStatus == vfs::VFS_OK) {
                vfs::FileInfo destinationInfo{};
                if (vfs::stat(destinationPath, &destinationInfo) != vfs::VFS_OK ||
                    destinationInfo.type != sourceInfo.type ||
                    (sourceInfo.type == vfs::FILE_TYPE_REGULAR && destinationInfo.size != sourceInfo.size)) {
                    return PasteResult::Failed;
                }
                clear();
                ++s_operationGeneration;
                return PasteResult::Success;
            }
        }

        // Cross-filesystem moves, and filesystems without atomic rename, use
        // copy-then-delete. The source is never deleted until the destination
        // tree has been created successfully.
        PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo);
        if (copied != PasteResult::Success) return copied;
        vfs::FileInfo destinationInfo{};
        if (vfs::stat(destinationPath, &destinationInfo) != vfs::VFS_OK ||
            destinationInfo.type != sourceInfo.type ||
            (sourceInfo.type == vfs::FILE_TYPE_REGULAR && destinationInfo.size != sourceInfo.size)) {
            remove_entry_tree(destinationPath);
            return PasteResult::Failed;
        }
        PasteResult removed = remove_entry_tree(s_sourcePath);
        if (removed != PasteResult::Success) {
            remove_entry_tree(destinationPath);
            return PasteResult::Failed;
        }
        clear();
        ++s_operationGeneration;
        return PasteResult::Success;
    }

    PasteResult copied = copy_entry_tree(s_sourcePath, destinationPath, sourceInfo);
    if (copied == PasteResult::Success) ++s_operationGeneration;
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
    switch (result) {
        case PasteResult::Success: return "Pasted item";
        case PasteResult::Empty: return "Clipboard is empty";
        case PasteResult::SourceMissing: return "Clipboard source is unavailable";
        case PasteResult::DestinationMissing: return "Paste destination is unavailable";
        case PasteResult::ReadOnly: return "Paste destination is read-only";
        case PasteResult::Conflict: return "Could not choose a safe file name";
        case PasteResult::Unsupported: return "Paste is unsupported for this item";
        case PasteResult::Failed: return "Paste failed";
        default: return "Paste failed";
    }
}

#if defined(GXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
static bool smoke_write_file(const char* path, const uint8_t* bytes, size_t byteCount) {
    return path && bytes && vfs::write_file(path, bytes, static_cast<uint32_t>(byteCount)) ==
        static_cast<int32_t>(byteCount);
}

static bool smoke_file_equals(const char* path, const uint8_t* expected, size_t expectedSize) {
    if (!path || !expected) return false;
    vfs::FileInfo info{};
    if (vfs::stat(path, &info) != vfs::VFS_OK || info.type != vfs::FILE_TYPE_REGULAR ||
        info.size != expectedSize) return false;
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

void run_runtime_smoke() {
    static const uint8_t sourceBytes[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
    static const uint8_t existingBytes[] = {0xCA, 0xFE};
    static const char* root = "/Desktop/GXOPSMK";
    static const char* destination = "/Desktop/GXOPSMK/OPDST";
    static const char* folder = "/Desktop/GXOPSMK/OPFOLD";
    static const char* nested = "/Desktop/GXOPSMK/OPFOLD/SUBDIR";
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
    ok &= vfs::mkdir("/Desktop/GXOPSMK/MISSING/TEST") == vfs::VFS_ERR_NOT_FOUND;

    const vfs::Status readOnlyStatus = vfs::mkdir("/system/TEST");
    serial::puts("[FILE-OPS-RUNTIME-SMOKE] readOnlyMount status=");
    serial::puts(vfs::status_name(readOnlyStatus));
    serial::puts("\n");
    ok &= readOnlyStatus == vfs::VFS_ERR_READ_ONLY;

    char sourceFile[vfs::VFS_MAX_PATH];
    vfs::join_path(root, "SRC.TXT", sourceFile, sizeof(sourceFile));
    ok &= smoke_write_file(sourceFile, sourceBytes, sizeof(sourceBytes));

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
