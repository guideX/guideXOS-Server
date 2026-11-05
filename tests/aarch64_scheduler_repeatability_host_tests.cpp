#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kernel/common_scheduler.h"

namespace {

struct MockContext {
    uint64_t stack_base;
    uint64_t stack_size;
    kernel::arch::thread_entry_t entry;
    void* argument;
};

alignas(4096) static uint8_t g_stacks[16][0x2000];
static MockContext g_contexts[64];
static uint32_t g_stack_count = 0;
static uint32_t g_context_count = 0;
static uint32_t g_context_switches = 0;
static bool g_irq_enabled = false;
static kernel::scheduler::Task* g_wake_on_idle = nullptr;
static uint64_t g_context_switch_marker = 0;
static std::jmp_buf g_bootstrap_return;
static bool g_return_to_bootstrap_expected = false;
static uint64_t g_interrupt_frame[40];

static void allocate_pages(uint64_t pages, uint64_t* base)
{
    assert(base);
    if (pages != 2 || g_stack_count >= 16) {
        *base = 0;
        return;
    }
    uint8_t* stack = g_stacks[g_stack_count++];
    std::memset(stack, 0, sizeof(g_stacks[0]));
    *base = reinterpret_cast<uint64_t>(stack);
}

static void empty_task(void*) {}

static void simulate_return_to_bootstrap()
{
    g_return_to_bootstrap_expected = true;
    if (setjmp(g_bootstrap_return) == 0) kernel::scheduler::return_to_bootstrap();
}

static void simulate_task_exit_to_bootstrap()
{
    g_return_to_bootstrap_expected = true;
    if (setjmp(g_bootstrap_return) == 0) kernel::scheduler::thread_exit();
}

static void initialize_scheduler(uint64_t preemption_target)
{
    kernel::scheduler::Config config{ allocate_pages, 2, 1, preemption_target };
    assert(kernel::scheduler::initialize(config));
}

static void reset_mock_architecture()
{
    g_stack_count = 0;
    g_context_count = 0;
    g_context_switches = 0;
    g_irq_enabled = false;
    g_wake_on_idle = nullptr;
    g_return_to_bootstrap_expected = false;
    std::memset(g_contexts, 0, sizeof(g_contexts));
    std::memset(g_interrupt_frame, 0, sizeof(g_interrupt_frame));
}

} // namespace

namespace kernel {
namespace arch {

interrupt_state_t irq_save()
{
    const interrupt_state_t previous = g_irq_enabled ? 1 : 0;
    g_irq_enabled = false;
    return previous;
}

void irq_restore(interrupt_state_t state) { g_irq_enabled = state != 0; }
void irq_enable() { g_irq_enabled = true; }
void irq_disable() { g_irq_enabled = false; }

void idle()
{
    if (g_wake_on_idle) {
        kernel::scheduler::Task* task = g_wake_on_idle;
        g_wake_on_idle = nullptr;
        kernel::scheduler::wake_task_locked(task);
    }
}

void memory_barrier() {}
const char* interface_name() { return "host-simulated-arm64"; }

void* context_init(uint64_t stack_base, uint64_t stack_size,
                   thread_entry_t entry, void* argument)
{
    if (!entry || g_context_count >= 64) return nullptr;
    MockContext* context = &g_contexts[g_context_count++];
    context->stack_base = stack_base;
    context->stack_size = stack_size;
    context->entry = entry;
    context->argument = argument;
    return context;
}

void context_switch(void** old_context, void* new_context)
{
    ++g_context_switches;
    if (g_return_to_bootstrap_expected && new_context == &g_context_switch_marker) {
        g_return_to_bootstrap_expected = false;
        std::longjmp(g_bootstrap_return, 1);
    }
    if (old_context) *old_context = &g_context_switch_marker;
}

bool interrupt_frame_save(void* frame, void* destination)
{
    if (!frame || !destination) return false;
    std::memset(destination, 0, kInterruptContextWords * sizeof(uint64_t));
    static_cast<uint64_t*>(destination)[0] = UINT64_C(0x53494d554c415445);
    static_cast<uint64_t*>(destination)[1] = reinterpret_cast<uint64_t>(frame);
    return true;
}

bool context_to_interrupt(void* context, void* destination)
{
    if (!context || !destination) return false;
    std::memset(destination, 0, kInterruptContextWords * sizeof(uint64_t));
    static_cast<uint64_t*>(destination)[0] = reinterpret_cast<uint64_t>(context);
    return true;
}

} // namespace arch
} // namespace kernel

int main()
{
    reset_mock_architecture();
    initialize_scheduler(6);
    kernel::scheduler::Task* task_a = kernel::scheduler::create_task(1, "task-A", empty_task, nullptr, true);
    kernel::scheduler::Task* task_b = kernel::scheduler::create_task(2, "task-B", empty_task, nullptr, true);
    kernel::scheduler::Task* task_c = kernel::scheduler::create_task(3, "task-C", empty_task, nullptr, true);
    kernel::scheduler::Task* fs_task = kernel::scheduler::create_task(4, "vfs-worker", empty_task, nullptr, false);
    kernel::scheduler::Task* completion = kernel::scheduler::create_task(99, "completion", empty_task, nullptr, false);
    assert(task_a && task_b && task_c && fs_task && completion);

    // Run the production scheduler's initial cooperative continuation through
    // mock architecture calls, then return to the bootstrap context.
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_COOPERATIVE);
    kernel::scheduler::start();
    assert(kernel::scheduler::current_task() != nullptr);
    assert(kernel::scheduler::current_task() != task_a || task_a->context != nullptr);
    simulate_return_to_bootstrap();
    assert(kernel::scheduler::current_task()->id == 0);

    assert(kernel::scheduler::reset_task(task_a, empty_task, nullptr, true));
    assert(kernel::scheduler::reset_task(task_b, empty_task, nullptr, true));
    assert(kernel::scheduler::reset_task(task_c, empty_task, nullptr, true));
    assert(kernel::scheduler::reset_task(fs_task, empty_task, nullptr, true));
    assert(kernel::scheduler::reset_task(completion, empty_task, nullptr, true));
    kernel::scheduler::set_completion_task(completion);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_PREEMPTIVE);
    assert(kernel::scheduler::prepare_interrupt_contexts());
    kernel::scheduler::start();
    assert(kernel::scheduler::current_task() != nullptr);

    // This is a simulated interrupt, not native ARM64 exception execution.
    // Rotate until the VFS worker is selected and inject a timer while a
    // synthetic VFS operation is marked active.
    uint32_t steps = 0;
    while (kernel::scheduler::current_task()->id != 4 && steps < 6) {
        assert(kernel::scheduler::timer_interrupt(g_interrupt_frame) != nullptr);
        ++steps;
    }
    assert(kernel::scheduler::current_task() == fs_task);
    volatile uint32_t simulated_vfs_stage = 1; // open/read operation in progress
    const uint64_t before = kernel::scheduler::preemptions();
    void* next = kernel::scheduler::timer_interrupt(g_interrupt_frame);
    assert(next != nullptr);
    assert(kernel::scheduler::preemptions() == before + 1);
    assert(simulated_vfs_stage == 1);
    assert(kernel::scheduler::current_task() != fs_task);

    // The production wait/wake transition is exercised with a simulated idle
    // callback that wakes the blocked task.  No native IRQ nesting is claimed.
    kernel::scheduler::Task* waiting = kernel::scheduler::current_task();
    g_wake_on_idle = waiting;
    assert(kernel::scheduler::block_current_locked());
    assert(waiting->state == kernel::scheduler::TASK_RUNNABLE);
    kernel::scheduler::Stats stats{};
    kernel::scheduler::get_stats(&stats);
    assert(stats.waits == 1 && stats.wakes == 1);
    assert(task_a->interrupt_context[0] != task_b->interrupt_context[0]);
    assert(g_context_switches >= 3);

    while (!kernel::scheduler::preemptive_complete()) {
        assert(kernel::scheduler::timer_interrupt(g_interrupt_frame) != nullptr);
    }
    assert(kernel::scheduler::current_task() == completion);
    assert(kernel::scheduler::preemptions() == 6);
    simulate_return_to_bootstrap();
    assert(kernel::scheduler::current_task()->id == 0);

    // Finished tasks cannot be revived by a stale wake, and repeated
    // scheduler initialization clears task/stat ownership for the next run.
    reset_mock_architecture();
    initialize_scheduler(2);
    task_a = kernel::scheduler::create_task(7, "teardown", empty_task, nullptr, true);
    assert(task_a);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_COOPERATIVE);
    kernel::scheduler::start();
    simulate_task_exit_to_bootstrap();
    assert(task_a->state == kernel::scheduler::TASK_FINISHED);
    kernel::scheduler::wake_task(task_a);
    assert(task_a->state == kernel::scheduler::TASK_FINISHED);

    reset_mock_architecture();
    initialize_scheduler(2);
    task_a = kernel::scheduler::create_task(42, "reinitialized", empty_task, nullptr, true);
    assert(task_a);
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_COOPERATIVE);
    kernel::scheduler::start();
    simulate_return_to_bootstrap();
    kernel::scheduler::get_stats(&stats);
    assert(stats.timer_ticks == 0 && stats.context_switches == 2);

    std::printf("AARCH64_SCHEDR1_HOST_PASS (simulated scheduler/IRQ paths; no native EL1 execution)\n");
    return 0;
}
