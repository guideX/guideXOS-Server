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

static bool append_text(char* destination, size_t destinationSize, size_t& length, const char* source) {
    if (!destination || !source || length >= destinationSize) return false;
    size_t sourceLength = local_strlen(source);
    if (sourceLength >= destinationSize - length) return false;
    for (size_t i = 0; i < sourceLength; ++i) destination[length + i] = source[i];
    length += sourceLength;
    destination[length] = '\0';
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

static bool source_is_valid(vfs::FileInfo* info) {
    if (s_operation == Operation::None || !s_sourcePath[0]) return false;
    vfs::FileInfo localInfo{};
    if (vfs::stat(s_sourcePath, &localInfo) != vfs::VFS_OK) return false;
    if (localInfo.type != vfs::FILE_TYPE_REGULAR) return false;
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
    return mount && !mount->readOnly;
}

static bool make_candidate_name(const char* sourceName, int candidateIndex,
                                char* destinationName, size_t destinationNameSize) {
    if (!sourceName || !destinationName || destinationNameSize == 0) return false;
    destinationName[0] = '\0';
    if (candidateIndex == 0) return copy_text(destinationName, destinationNameSize, sourceName);

    const size_t nameLength = local_strlen(sourceName);
    size_t extensionOffset = nameLength;
    for (size_t i = nameLength; i > 0; --i) {
        if (sourceName[i - 1] == '.') {
            if (i > 1) extensionOffset = i - 1;
            break;
        }
    }

    const size_t extensionLength = nameLength - extensionOffset;
    const char* suffix = " - Copy";
    size_t suffixLength = local_strlen(suffix);
    char number[12] = {0};
    size_t numberLength = 0;
    if (candidateIndex >= 2) {
        size_t numberWrite = 0;
        if (!append_decimal(number, sizeof(number), numberWrite, candidateIndex)) return false;
        numberLength = numberWrite;
    }

    const size_t fixedLength = suffixLength + (candidateIndex >= 2 ? 1 + numberLength : 0) + extensionLength;
    if (fixedLength + 1 >= destinationNameSize) return false;

    size_t baseLength = extensionOffset;
    if (baseLength + fixedLength + 1 >= destinationNameSize) {
        baseLength = destinationNameSize - fixedLength - 2;
    }

    size_t outputLength = 0;
    for (size_t i = 0; i < baseLength; ++i) destinationName[outputLength++] = sourceName[i];
    destinationName[outputLength] = '\0';
    if (!append_text(destinationName, destinationNameSize, outputLength, suffix)) return false;
    if (candidateIndex >= 2) {
        if (!append_text(destinationName, destinationNameSize, outputLength, " ") ||
            !append_text(destinationName, destinationNameSize, outputLength, number)) return false;
    }
    if (extensionLength > 0) {
        if (outputLength + extensionLength >= destinationNameSize) return false;
        for (size_t i = 0; i < extensionLength; ++i) {
            destinationName[outputLength++] = sourceName[extensionOffset + i];
        }
        destinationName[outputLength] = '\0';
    }
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

bool set_file(const char* sourcePath, Operation operation) {
    if (!sourcePath || !sourcePath[0] || operation == Operation::None) return false;

    char normalizedPath[vfs::VFS_MAX_PATH];
    vfs::normalize_path(sourcePath, normalizedPath, sizeof(normalizedPath));
    vfs::FileInfo info{};
    if (!normalizedPath[0] || vfs::stat(normalizedPath, &info) != vfs::VFS_OK ||
        info.type != vfs::FILE_TYPE_REGULAR) {
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

bool can_paste_to(const char* destinationDirectory) {
    if (!source_is_valid(nullptr)) return false;
    char normalizedDestination[vfs::VFS_MAX_PATH];
    return destination_is_valid(destinationDirectory, normalizedDestination, sizeof(normalizedDestination));
}

PasteResult paste_to_directory(const char* destinationDirectory) {
    if (!has_pending_file()) return PasteResult::Empty;

    vfs::FileInfo sourceInfo{};
    if (!source_is_valid(&sourceInfo)) return PasteResult::SourceMissing;

    char normalizedDestination[vfs::VFS_MAX_PATH];
    if (!destination_is_valid(destinationDirectory, normalizedDestination, sizeof(normalizedDestination))) {
        vfs::FileInfo destinationInfo{};
        if (!destinationDirectory || vfs::stat(destinationDirectory, &destinationInfo) != vfs::VFS_OK ||
            destinationInfo.type != vfs::FILE_TYPE_DIRECTORY) {
            return PasteResult::DestinationMissing;
        }
        return PasteResult::ReadOnly;
    }

    char sourceDestinationPath[vfs::VFS_MAX_PATH];
    vfs::join_path(normalizedDestination, s_sourceName, sourceDestinationPath, sizeof(sourceDestinationPath));
    if (!sourceDestinationPath[0]) return PasteResult::Unsupported;

    // Cutting a file into its current directory is a safe no-op. Copying to
    // the same directory deliberately takes the deterministic Copy name path.
    if (s_operation == Operation::Move && same_path(s_sourcePath, sourceDestinationPath)) {
        clear();
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
                clear();
                return PasteResult::Success;
            }
        }

        // Cross-filesystem moves, and filesystems without atomic rename, use
        // copy-then-delete. The source is never deleted until the destination
        // has been created and size-verified.
        PasteResult copied = copy_file_contents(s_sourcePath, destinationPath, sourceInfo.size);
        if (copied != PasteResult::Success) return copied;
        if (vfs::unlink(s_sourcePath) != vfs::VFS_OK) {
            vfs::unlink(destinationPath);
            return PasteResult::Failed;
        }
        clear();
        return PasteResult::Success;
    }

    return copy_file_contents(s_sourcePath, destinationPath, sourceInfo.size);
}

const char* paste_result_message(PasteResult result) {
    switch (result) {
        case PasteResult::Success: return "Pasted file";
        case PasteResult::Empty: return "Clipboard is empty";
        case PasteResult::SourceMissing: return "Clipboard source is unavailable";
        case PasteResult::DestinationMissing: return "Paste destination is unavailable";
        case PasteResult::ReadOnly: return "Paste destination is read-only";
        case PasteResult::Conflict: return "Could not choose a safe file name";
        case PasteResult::Unsupported: return "File paste is unsupported for this file";
        case PasteResult::Failed: return "File paste failed";
        default: return "File paste failed";
    }
}

} // namespace file_clipboard
} // namespace kernel
