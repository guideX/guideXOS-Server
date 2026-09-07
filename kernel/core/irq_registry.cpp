#include "include/kernel/irq_registry.h"

namespace kernel {
namespace irq {

namespace {
static const uint32_t kMaxIrqs = 64;
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
