//
// guideXOS bare-metal file clipboard
//
// Shared by the kernel File Explorer and desktop shell surfaces. This is a
// file-operation clipboard, not the host clipboard and not the text clipboard.
//

#ifndef KERNEL_FILE_CLIPBOARD_H
#define KERNEL_FILE_CLIPBOARD_H

#include "kernel/types.h"
#include "kernel/vfs.h"

namespace kernel {
namespace file_clipboard {

enum class Operation : uint8_t {
    None,
    Copy,
    Move,
};

enum class PasteResult : uint8_t {
    Success,
    Empty,
    SourceMissing,
    DestinationMissing,
    ReadOnly,
    Conflict,
    Unsupported,
    Failed,
};

enum class PasteStage : uint8_t {
    None,
    ClipboardSet,
    SourceValidation,
    DestinationValidation,
    DestinationNaming,
    SourceOpenRead,
    DestinationCreate,
    DataTransfer,
    Flush,
    Verification,
    Refresh,
    Complete,
};

struct PasteDiagnostic {
    PasteStage stage{PasteStage::None};
    PasteResult result{PasteResult::Failed};
    vfs::Status vfsStatus{vfs::VFS_OK};
    char fatStatus[48]{};
    char sourcePath[vfs::VFS_MAX_PATH]{};
    char destinationDirectory[vfs::VFS_MAX_PATH]{};
    char destinationPath[vfs::VFS_MAX_PATH]{};
    uint64_t bytesExpected{0};
    uint64_t bytesRead{0};
    uint64_t bytesWritten{0};
};

// Record one VFS file or directory in the kernel file-operation clipboard.
bool set_file(const char* sourcePath, Operation operation);

// Clear a completed or invalid operation.
void clear();

bool has_pending_file();
Operation pending_operation();

// Monotonic generation for completed copy/move operations. File Explorer
// uses this to refresh when the desktop consumes the shared clipboard.
uint64_t operation_generation();

#if defined(GXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
// Diagnostic-only VFS production-path smoke coverage. This is compiled and
// invoked only by the opt-in QEMU runtime smoke build.
void run_runtime_smoke();
#endif

// Returns true only when the source and destination are currently suitable
// for a file or folder paste. The operation itself still performs all
// validation again.
bool can_paste_to(const char* destinationDirectory);

// Copy or move the pending file or folder into a destination directory.
PasteResult paste_to_directory(const char* destinationDirectory);

// Create a collision-free desktop-style folder name through the VFS. The
// returned path is the actual path created by the current filesystem backend.
bool create_unique_folder(const char* destinationDirectory, char* outPath, size_t outPathSize);

// Diagnostic form used by the Desktop command. It returns the exact VFS
// status from the shared create operation while preserving the legacy wrapper.
bool create_unique_folder_ex(const char* destinationDirectory,
                             char* outPath,
                             size_t outPathSize,
                             vfs::Status* outStatus);

const char* paste_result_message(PasteResult result);
const char* paste_stage_name(PasteStage stage);
const PasteDiagnostic& last_paste_diagnostic();
const char* paste_diagnostic_message();
void note_paste_refresh(bool success);

} // namespace file_clipboard
} // namespace kernel

#endif // KERNEL_FILE_CLIPBOARD_H
