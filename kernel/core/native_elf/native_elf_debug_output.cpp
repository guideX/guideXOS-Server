#include "native_elf_debug_output.h"

namespace kernel {
namespace native_elf {

void NativeDebugOutputQueue::clear()
{
    head = 0;
    count = 0;
    dropped = 0;
    for (uint32_t i = 0; i < GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS; ++i)
        records[i] = gx_development_debug_output_record();
}

bool NativeDebugOutputQueue::enqueue(const gx_development_debug_output_record& record)
{
    if (count >= GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS) {
        if (dropped != 0xFFFFFFFFU) ++dropped;
        return false;
    }
    const uint32_t slot = (head + count) % GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS;
    records[slot] = record;
    ++count;
    return true;
}

uint32_t NativeDebugOutputQueue::drain(
    gx_development_debug_output_record* destination, uint32_t capacity)
{
    const uint32_t available = count;
    const uint32_t copied = available < capacity ? available : capacity;
    if (destination) {
        for (uint32_t i = 0; i < copied; ++i) {
            const uint32_t slot = (head + i) % GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS;
            destination[i] = records[slot];
        }
    }
    head = 0;
    count = 0;
    return copied;
}

} // namespace native_elf
} // namespace kernel
