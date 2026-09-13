#pragma once

#include <stdint.h>

#include "../../../sdk/include/guidexos/development_debug.h"

namespace kernel {
namespace native_elf {

struct NativeDebugOutputQueue {
    uint32_t head;
    uint32_t count;
    uint32_t dropped;
    gx_development_debug_output_record records[GX_DEVELOPMENT_DEBUG_MAX_OUTPUT_RECORDS];

    void clear();
    bool enqueue(const gx_development_debug_output_record& record);
    uint32_t drain(gx_development_debug_output_record* destination, uint32_t capacity);
};

} // namespace native_elf
} // namespace kernel
