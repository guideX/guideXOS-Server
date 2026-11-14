#include "arch/context_switch.h"

using kernel::arch::amd64::context::SwitchContext;
using kernel::arch::amd64::context::init_context;
using kernel::arch::amd64::context::switch_context;

struct TaskState {
    uint32_t phase;
    uint32_t failure;
    uint32_t entryStackModulo;
    uint64_t preservedMask;
};
static_assert(offsetof(TaskState, entryStackModulo) == 8, "entry alignment probe offset must match NASM");

static SwitchContext* g_mainContext = nullptr;
static SwitchContext* g_taskContext = nullptr;
static TaskState g_taskState = {};
static uint8_t g_taskStack[4096] __attribute__((aligned(16)));

extern "C" uint64_t GXOS_AMD64_SWITCH_ABI context_switch_preservation_probe(
    SwitchContext** oldContext, SwitchContext* newContext);
extern "C" void context_test_task_entry(void* argument);

extern "C" void task_entry_body(void* argument)
{
    if (argument != &g_taskState) {
        g_taskState.failure = 1;
    }

    g_taskState.phase = 1;
    switch_context(&g_taskContext, g_mainContext);

    g_taskState.phase = 2;
    g_taskState.preservedMask = context_switch_preservation_probe(&g_taskContext, g_mainContext);

    g_taskState.phase = 3;
    switch_context(&g_taskContext, g_mainContext);

    // The test resumes this point only if the task unexpectedly gets resumed.
    g_taskState.failure = 2;
    for (;;) { }
}

int main()
{
    const uint64_t stackBase = reinterpret_cast<uint64_t>(&g_taskStack[0]);
    const uint64_t stackSize = sizeof(g_taskStack);
    const uint64_t alignedTop = (stackBase + stackSize) & ~static_cast<uint64_t>(0xFULL);

    if (init_context(stackBase, 119u, context_test_task_entry, &g_taskState) != nullptr) {
        return 1;
    }
    if (init_context(stackBase, stackSize, nullptr, &g_taskState) != nullptr) {
        return 2;
    }
    if (init_context(~static_cast<uint64_t>(0) - 63u, 128u,
                     context_test_task_entry, &g_taskState) != nullptr) {
        return 3;
    }

    g_taskContext = init_context(stackBase, stackSize, context_test_task_entry, &g_taskState);
    if (!g_taskContext ||
        reinterpret_cast<uint64_t>(g_taskContext) != alignedTop - sizeof(SwitchContext) - 40u ||
        g_taskContext->rsp != alignedTop - 40u ||
        g_taskContext->r12 != reinterpret_cast<uint64_t>(&context_test_task_entry) ||
        g_taskContext->r13 != reinterpret_cast<uint64_t>(&g_taskState)) {
        return 4;
    }

    const uint64_t mainMask = context_switch_preservation_probe(&g_mainContext, g_taskContext);
    if (mainMask != 0xFFu || g_taskState.phase != 1 || g_taskState.failure != 0 ||
        g_taskState.entryStackModulo != 8u) {
        return 5;
    }

    switch_context(&g_mainContext, g_taskContext);
    if (g_taskState.phase != 2 || g_taskState.failure != 0) {
        return 6;
    }

    switch_context(&g_mainContext, g_taskContext);
    if (g_taskState.phase != 3 || g_taskState.failure != 0 || g_taskState.preservedMask != 0xFFu) {
        return 7;
    }

    return 0;
}
