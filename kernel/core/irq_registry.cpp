#include "include/kernel/irq_registry.h"

namespace kernel {
namespace irq {

namespace {
// GICv2 SPI numbers used by QEMU virtio-MMIO devices are in the 32..95
// range.  Keep the common registry large enough for those platform IRQs;
// callers still receive a bounded failure for IDs outside this table.
static const uint32_t kMaxIrqs = 128;
struct Entry { Handler handler; void* context; uint64_t dispatches; };
static Entry g_entries[kMaxIrqs];
}

bool initialize()
{
    for (uint32_t i = 0; i < kMaxIrqs; ++i) {
        g_entries[i].handler = nullptr;
        g_entries[i].context = nullptr;
        g_entries[i].dispatches = 0;
    }
    return true;
}

bool register_handler(uint32_t irq, Handler handler, void* context)
{
    if (irq >= kMaxIrqs || !handler) return false;
    g_entries[irq] = { handler, context, 0 };
    return true;
}

bool has_handler(uint32_t irq)
{
    return irq < kMaxIrqs && g_entries[irq].handler != nullptr;
}

void* dispatch(uint32_t irq, void* frame)
{
    if (irq >= kMaxIrqs || !g_entries[irq].handler) return nullptr;
    ++g_entries[irq].dispatches;
    return g_entries[irq].handler(irq, frame, g_entries[irq].context);
}

uint64_t dispatch_count(uint32_t irq)
{
    return irq < kMaxIrqs ? g_entries[irq].dispatches : 0;
}

} // namespace irq
} // namespace kernel
