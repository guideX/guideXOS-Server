// Architecture-neutral guideXOS kernel scheduler.
//
// The scheduler owns task lifecycle and runnable selection.  Register save,
// interrupt-frame representation, interrupt masking, and CPU idle are all
// delegated through arch_interface.h.

#include "include/kernel/common_scheduler.h"
#include "include/kernel/scheduler_contract.h"

namespace kernel {
namespace scheduler {

namespace {

static const uint32_t kMaxTasks = 8;
static const uint64_t kLowCanary = UINT64_C(0x47584f535441434b);
static const uint64_t kHighCanary = UINT64_C(0x43414e415259454e);

static Config s_config{};
static Task s_tasks[kMaxTasks]{};
static uint32_t s_task_count = 0;
static Task s_bootstrap{};
static void* s_bootstrap_context = 0;
static Task* s_current = 0;
static Task* s_completion_task = 0;
static Task* s_idle_task = 0;
static uint32_t s_round_robin_cursor = 0;
static Phase s_phase = PHASE_COOPERATIVE;
static bool s_cooperative_complete = false;
static bool s_preemptive_complete = false;
static bool s_failed = false;
static Stats s_stats{};

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

static bool is_real_task(const Task* task)
{
    return task != 0 && task != &s_bootstrap;
}

static bool task_stack_valid(const Task* task)
{
    if (!is_real_task(task) ||
        !gxos_scheduler_stack_layout_valid(task->stack_base, task->stack_size, task->stack_top)) {
        return false;
    }
    volatile uint64_t* low = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(task->stack_base));
    volatile uint64_t* high = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(task->stack_base + task->stack_size - sizeof(uint64_t)));
    return gxos_scheduler_canaries_valid(*low, *high, task->low_canary, task->high_canary);
}

static bool stack_ranges_overlap(uint64_t left_base, uint64_t left_size,
                                 uint64_t right_base, uint64_t right_size)
{
    uint64_t left_end = 0;
    uint64_t right_end = 0;
    if (!gxos_scheduler_add_u64(left_base, left_size, &left_end) ||
        !gxos_scheduler_add_u64(right_base, right_size, &right_end)) return true;
    return left_base < right_end && right_base < left_end;
}

static void mark_failure()
{
    s_failed = true;
}

static bool task_is_runnable(const Task* task)
{
    return is_real_task(task) && task->enabled != 0 && task->state == TASK_RUNNABLE &&
           task->context != 0;
}

static Task* find_next(Task* current, bool include_completion)
{
    if (s_task_count == 0) return 0;
    for (uint32_t offset = 1; offset <= s_task_count; ++offset) {
        const uint32_t index = (s_round_robin_cursor + offset) % s_task_count;
        Task* candidate = &s_tasks[index];
        if (candidate == current || !task_is_runnable(candidate)) continue;
        if (!include_completion && candidate == s_completion_task) continue;
        s_round_robin_cursor = index;
        return candidate;
    }
    return task_is_runnable(current) ? current : 0;
}

static void count_switch(Task* next)
{
    s_current = next;
    ++s_stats.context_switches;
    if (s_phase == PHASE_COOPERATIVE) ++s_stats.cooperative_switches;
}

} // namespace

bool initialize(const Config& config)
{
    if (!config.allocate_pages || config.stack_pages < 2 ||
        config.cooperative_switch_target == 0 || config.preemption_target == 0) return false;

    s_config = config;
    zero_bytes(s_tasks, sizeof(s_tasks));
    zero_bytes(&s_bootstrap, sizeof(s_bootstrap));
    zero_bytes(&s_stats, sizeof(s_stats));
    s_task_count = 0;
    s_bootstrap.id = 0;
    s_bootstrap.name = "bootstrap";
    s_bootstrap.enabled = 1;
    s_bootstrap.state = TASK_RUNNABLE;
    s_bootstrap_context = 0;
    s_current = &s_bootstrap;
    s_completion_task = 0;
    s_idle_task = 0;
    s_round_robin_cursor = 0;
    s_phase = PHASE_COOPERATIVE;
    s_cooperative_complete = false;
    s_preemptive_complete = false;
    s_failed = false;
    return true;
}

Task* create_task(uint32_t id, const char* name, kernel::arch::thread_entry_t entry,
                  void* argument, bool enabled)
{
    if (s_task_count >= kMaxTasks || entry == 0) return 0;

    if (s_config.stack_pages > UINT64_MAX / UINT64_C(0x1000)) return 0;
    uint64_t stack_size = 0;
    if (!gxos_scheduler_add_u64(0, s_config.stack_pages * UINT64_C(0x1000), &stack_size) ||
        stack_size < UINT64_C(0x2000)) return 0;

    Task* task = &s_tasks[s_task_count];
    uint64_t stack_base = 0;
    s_config.allocate_pages(s_config.stack_pages, &stack_base);
    uint64_t stack_end = 0;
    if (!gxos_scheduler_add_u64(stack_base, stack_size, &stack_end) ||
        stack_end < UINT64_C(0x10) ||
        !gxos_scheduler_stack_layout_valid(stack_base, stack_size,
                                           stack_end - UINT64_C(0x10))) return 0;
    for (uint32_t i = 0; i < s_task_count; ++i) {
        if (stack_ranges_overlap(stack_base, stack_size,
                                 s_tasks[i].stack_base, s_tasks[i].stack_size)) return 0;
    }

    task->id = id;
    task->name = name;
    task->entry = entry;
    task->argument = argument;
    task->stack_base = stack_base;
    task->stack_size = stack_size;
    task->stack_top = stack_end - UINT64_C(0x10);
    task->low_canary = kLowCanary ^ static_cast<uint64_t>(id);
    task->high_canary = kHighCanary ^ static_cast<uint64_t>(id);
    task->context = kernel::arch::context_init(task->stack_base, task->stack_size,
                                               task->entry, task->argument);
    if (!task->context) {
        zero_bytes(task, sizeof(*task));
        return 0;
    }
    volatile uint64_t* low = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(task->stack_base));
    volatile uint64_t* high = reinterpret_cast<volatile uint64_t*>(
        static_cast<uintptr_t>(task->stack_base + task->stack_size - sizeof(uint64_t)));
    *low = task->low_canary;
    *high = task->high_canary;
    task->enabled = enabled ? 1 : 0;
    task->state = enabled ? TASK_RUNNABLE : TASK_BLOCKED;
    ++s_task_count;
    return task;
}

bool reset_task(Task* task, kernel::arch::thread_entry_t entry, void* argument, bool enabled)
{
    if (!is_real_task(task) || entry == 0 || !task_stack_valid(task)) {
        mark_failure();
        return false;
    }
    task->entry = entry;
    task->argument = argument;
    task->context = kernel::arch::context_init(task->stack_base, task->stack_size,
                                               task->entry, task->argument);
    if (!task->context) {
        mark_failure();
        return false;
    }
    zero_bytes(task->interrupt_context, sizeof(task->interrupt_context));
    task->interrupt_context_valid = 0;
    task->enabled = enabled ? 1 : 0;
    task->state = enabled ? TASK_RUNNABLE : TASK_BLOCKED;
    task->execution_count = 0;
    task->preemption_count = 0;
    return true;
}

void set_phase(Phase phase)
{
    s_phase = phase;
}

void set_completion_task(Task* task)
{
    s_completion_task = task;
}

void set_idle_task(Task* task)
{
    s_idle_task = task;
}

bool prepare_interrupt_contexts()
{
    for (uint32_t i = 0; i < s_task_count; ++i) {
        Task* task = &s_tasks[i];
        if (!task->enabled) continue;
        if (!task->context || !kernel::arch::context_to_interrupt(
                task->context, task->interrupt_context)) {
            mark_failure();
            return false;
        }
        task->interrupt_context_valid = 1;
    }
    return !s_failed;
}

void start()
{
    Task* next = 0;
    if (s_phase == PHASE_IDLE) {
        next = s_idle_task;
    } else {
        next = find_next(&s_bootstrap, false);
    }
    // The first start has no saved bootstrap context yet; context_switch
    // fills it while entering the first task.
    if (!task_is_runnable(next) || s_failed) return;

    const kernel::arch::interrupt_state_t state = kernel::arch::irq_save();
    count_switch(next);
    // The destination task must observe the caller's interrupt state.  This
    // is especially important for the first preemptive task: keeping DAIF.I
    // masked until context_switch returns would make the timer unreachable.
    kernel::arch::irq_restore(state);
    kernel::arch::context_switch(&s_bootstrap_context, next->context);
}

void yield()
{
    if (!is_real_task(s_current) || s_failed) return;
    const kernel::arch::interrupt_state_t state = kernel::arch::irq_save();
    Task* old = s_current;
    Task* next = 0;
    if (s_phase == PHASE_COOPERATIVE && s_cooperative_complete) {
        next = &s_bootstrap;
    } else if (s_phase == PHASE_IDLE) {
        next = &s_bootstrap;
    } else {
        next = find_next(old, false);
    }
    if (!next || next == old) {
        kernel::arch::irq_restore(state);
        return;
    }
    if (is_real_task(old) && !task_stack_valid(old)) mark_failure();
    count_switch(next);
    kernel::arch::context_switch(&old->context, next == &s_bootstrap
                                                   ? s_bootstrap_context
                                                   : next->context);
    kernel::arch::irq_restore(state);
}

void mark_cooperative_complete()
{
    s_cooperative_complete = true;
}

void* timer_interrupt(void* frame)
{
    ++s_stats.timer_ticks;
    if (s_phase == PHASE_IDLE) {
        ++s_stats.idle_wake_ticks;
        return 0;
    }
    if (s_phase != PHASE_PREEMPTIVE || !is_real_task(s_current) || s_preemptive_complete) {
        return 0;
    }

    Task* current = s_current;
    if (!task_stack_valid(current) || !frame ||
        !kernel::arch::interrupt_frame_save(frame, current->interrupt_context)) {
        mark_failure();
        return 0;
    }
    current->interrupt_context_valid = 1;
    ++current->preemption_count;
    ++s_stats.preemptions;

    if (s_stats.preemptions >= s_config.preemption_target && s_completion_task &&
        s_completion_task->enabled && s_completion_task->interrupt_context_valid) {
        s_preemptive_complete = true;
        count_switch(s_completion_task);
        return s_completion_task->interrupt_context;
    }

    Task* next = find_next(current, false);
    if (!next || next == current || !next->interrupt_context_valid) return 0;
    count_switch(next);
    return next->interrupt_context;
}

void return_to_bootstrap()
{
    if (!is_real_task(s_current) || !s_bootstrap_context) {
        for (;;) kernel::arch::idle();
    }
    Task* old = s_current;
    const kernel::arch::interrupt_state_t state = kernel::arch::irq_save();
    (void)state;
    count_switch(&s_bootstrap);
    kernel::arch::context_switch(&old->context, s_bootstrap_context);
    for (;;) kernel::arch::idle();
}

void thread_exit()
{
    if (is_real_task(s_current)) {
        s_current->state = TASK_FINISHED;
        s_current->enabled = 0;
    }
    return_to_bootstrap();
}

void note_execution()
{
    if (is_real_task(s_current)) ++s_current->execution_count;
}

Task* current_task() { return s_current; }
Task* task_at(uint32_t index) { return index < s_task_count ? &s_tasks[index] : 0; }
uint32_t task_count() { return s_task_count; }
uint64_t cooperative_switches() { return s_stats.cooperative_switches; }
uint64_t preemptions() { return s_stats.preemptions; }
uint64_t idle_wake_ticks() { return s_stats.idle_wake_ticks; }
bool preemptive_complete() { return s_preemptive_complete; }
bool failed() { return s_failed; }

bool stack_integrity()
{
    if (s_failed) return false;
    for (uint32_t i = 0; i < s_task_count; ++i) {
        if (!task_stack_valid(&s_tasks[i])) return false;
    }
    return true;
}

void note_unexpected_irq()
{
    ++s_stats.unexpected_irqs;
    mark_failure();
}

void get_stats(Stats* stats)
{
    if (!stats) return;
    // Keep the common scheduler freestanding: aggregate assignment can lower
    // to a libc memcpy on some cross-compilers.
    stats->timer_ticks = s_stats.timer_ticks;
    stats->context_switches = s_stats.context_switches;
    stats->cooperative_switches = s_stats.cooperative_switches;
    stats->preemptions = s_stats.preemptions;
    stats->idle_wake_ticks = s_stats.idle_wake_ticks;
    stats->task_count = s_task_count;
    stats->unexpected_irqs = s_stats.unexpected_irqs;
}

} // namespace scheduler
} // namespace kernel

extern "C" void scheduler_yield()
{
    kernel::scheduler::yield();
}

extern "C" void scheduler_thread_exit()
{
    kernel::scheduler::thread_exit();
}
