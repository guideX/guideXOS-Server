#ifndef KERNEL_NATIVE_READ_PROGRESS_H
#define KERNEL_NATIVE_READ_PROGRESS_H

#include "kernel/types.h"

namespace kernel {
namespace native_read_progress {

static const uint64_t kCheckpointInterval = 1024u * 1024u;
static const uint32_t kMaxDiagnosticRecords = 80u;

struct State {
    uint64_t lastEnd;
    uint64_t sequentialBytes;
    uint64_t nextCheckpoint;
    bool hasLastEnd;
};

inline void reset(State& state)
{
    state.lastEnd = 0;
    state.sequentialBytes = 0;
    state.nextCheckpoint = 0;
    state.hasLastEnd = false;
}

// Return the number of crossed 1 MiB checkpoints and write the first one to
// outFirstCheckpoint. Only positive, contiguous successful reads count.
// A nonsequential read starts a new baseline and never claims prior bytes.
inline uint32_t observe(State& state, uint64_t offset, uint32_t bytesRead,
                        bool successful, uint64_t* outFirstCheckpoint)
{
    if (outFirstCheckpoint) *outFirstCheckpoint = 0;
    if (!successful) {
        reset(state);
        return 0;
    }
    if (bytesRead == 0) return 0;
    if (offset > UINT64_MAX - bytesRead) {
        reset(state);
        return 0;
    }

    const uint64_t end = offset + bytesRead;
    const bool contiguous = state.hasLastEnd && offset == state.lastEnd;
    if (!contiguous) {
        state.hasLastEnd = true;
        state.lastEnd = end;
        state.sequentialBytes = 0;
        state.nextCheckpoint = kCheckpointInterval;
        if (offset != 0) {
            return 0;
        }
    }

    if (state.sequentialBytes > UINT64_MAX - bytesRead) {
        reset(state);
        return 0;
    }
    state.lastEnd = end;
    state.sequentialBytes += bytesRead;

    if (state.nextCheckpoint == 0 || state.sequentialBytes < state.nextCheckpoint) return 0;
    const uint64_t first = state.nextCheckpoint;
    uint32_t count = 0;
    do {
        ++count;
        if (state.nextCheckpoint > UINT64_MAX - kCheckpointInterval) {
            state.nextCheckpoint = 0;
            break;
        }
        state.nextCheckpoint += kCheckpointInterval;
    } while (state.nextCheckpoint != 0 &&
             state.sequentialBytes >= state.nextCheckpoint);
    if (outFirstCheckpoint) *outFirstCheckpoint = first;
    return count;
}

} // namespace native_read_progress
} // namespace kernel

#endif // KERNEL_NATIVE_READ_PROGRESS_H
