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
    Busy,
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

// The filesystem operation remains synchronous, but its state is explicit so
// shell/UI code can disable conflicting commands and render progress without
// re-entering VFS or FAT mutation code.
enum class OperationState : uint8_t {
    Idle,
    Preparing,
    Copying,
    Moving,
    Verifying,
    Refreshing,
    Completed,
    Failed,
};

struct FileOperationProgress {
    Operation operation{Operation::None};
    OperationState state{OperationState::Idle};
    PasteStage phase{PasteStage::None};
    PasteResult result{PasteResult::Failed};
    char sourceDisplayName[vfs::VFS_MAX_FILENAME]{};
    char destinationPath[vfs::VFS_MAX_PATH]{};
    uint64_t totalBytes{0};
    uint64_t bytesCompleted{0};
    bool totalKnown{false};
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

using ProgressCallback = void (*)(const FileOperationProgress& progress);

// Register the shell-facing progress sink. The callback is invoked before
// filesystem work starts and at bounded transfer checkpoints; it must not
// mutate the filesystem or call paste_to_directory().
void set_progress_callback(ProgressCallback callback);

bool operation_active();
OperationState operation_state();
const FileOperationProgress& progress();

// Monotonic generation for completed copy/move operations. File Explorer
// uses this to refresh when the desktop consumes the shared clipboard.
uint64_t operation_generation();

#if defined(GXOS_FILE_OPERATIONS_RUNTIME_SMOKE_ACTIVE) && defined(GXOS_BARE_METAL)
// Diagnostic-only VFS production-path smoke coverage. This is compiled and
// invoked only by the opt-in QEMU runtime smoke build.
void run_runtime_smoke();
void run_trash_runtime_smoke();
#endif

// Returns true only when the source and destination are currently suitable
// for a file or folder paste. The operation itself still performs all
// validation again.
bool can_paste_to(const char* destinationDirectory);

// Copy or move the pending file or folder into a destination directory.
PasteResult paste_to_directory(const char* destinationDirectory);

// Move a validated VFS file or folder to the per-filesystem Trash directory.
// The mutation is guarded by the same operation state as paste, preserves
// Trash restore metadata, and returns the actual collision-safe destination.
PasteResult move_to_trash(const char* sourcePath,
                          char* outTrashedPath,
                          size_t outTrashedPathSize);

// FAT-safe paired restore metadata helpers used by the shared Move-to-Trash
// implementation and the bare-metal Trash UI. Metadata names are reserved
// short names, never dot-prefixed or long-name companions.
bool trash_metadata_path_for(const char* trashedPath,
                             char* outMetadataPath,
                             size_t outMetadataPathSize);
bool is_trash_metadata_name(const char* name);

// Keep the busy state through the caller-owned destination refresh. This is
// intentionally separate from paste_to_directory() so the filesystem layer
// does not depend on desktop refresh implementation details.
bool begin_paste_refresh();

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
