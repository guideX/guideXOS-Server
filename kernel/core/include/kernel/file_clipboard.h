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

// Record one regular file in the kernel file-operation clipboard.
bool set_file(const char* sourcePath, Operation operation);

// Clear a completed or invalid operation.
void clear();

bool has_pending_file();
Operation pending_operation();

// Returns true only when the source and destination are currently suitable
// for a file paste. The operation itself still performs all validation again.
bool can_paste_to(const char* destinationDirectory);

// Copy or move the pending regular file into a destination directory.
PasteResult paste_to_directory(const char* destinationDirectory);

const char* paste_result_message(PasteResult result);

} // namespace file_clipboard
} // namespace kernel

#endif // KERNEL_FILE_CLIPBOARD_H
