#pragma once

#include <stdint.h>

namespace kernel {
namespace irq {

typedef void* (*Handler)(uint32_t irq, void* frame, void* context);

bool initialize();
bool register_handler(uint32_t irq, Handler handler, void* context);
bool has_handler(uint32_t irq);
void* dispatch(uint32_t irq, void* frame);
uint64_t dispatch_count(uint32_t irq);

} // namespace irq
} // namespace kernel
