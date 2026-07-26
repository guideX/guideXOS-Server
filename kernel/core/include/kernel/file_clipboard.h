//
// guideXOS bare-metal file clipboard
//
// Shared by the kernel File Explorer and desktop shell surfaces. This is a
// file-operation clipboard, not the host clipboard and not the text clipboard.
//

#ifndef KERNEL_FILE_CLIPBOARD_H
#define KERNEL_FILE_CLIPBOARD_H

#include "kernel/types.h"

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

const char* paste_result_message(PasteResult result);

} // namespace file_clipboard
} // namespace kernel

#endif // KERNEL_FILE_CLIPBOARD_H
