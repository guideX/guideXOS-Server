// Architecture-neutral guideXOS kernel scheduler.
//
// The scheduler owns task lifecycle and runnable selection.  Register save,
// interrupt-frame representation, interrupt masking, and CPU idle are all
// delegated through arch_interface.h.

#include "include/kernel/common_scheduler.h"
#include "include/kernel/scheduler_contract.h"
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
#include "kernel/arch/arm64/phase4_diagnostics.h"
#endif

namespace kernel {
namespace scheduler {

namespace {

static const uint32_t kMaxTasks = 16;
static const uint64_t kLowCanary = UINT64_C(0x47584f535441434b);
static const uint64_t kHighCanary = UINT64_C(0x43414e415259454e);

static Config s_config{};
static Task s_tasks[kMaxTasks]{};
static uint32_t s_task_count = 0;
static uint32_t s_task_slots = 0;
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
    if (!task || task == &s_bootstrap || s_task_slots == 0) return false;
    const uintptr_t address = reinterpret_cast<uintptr_t>(task);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(&s_tasks[0]);
    const uintptr_t end = reinterpret_cast<uintptr_t>(&s_tasks[s_task_slots]);
    return address >= begin && address < end &&
           ((address - begin) % sizeof(Task)) == 0 &&
           task->state != TASK_UNUSED;
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
           task->context != 0 &&
           (task->execution_kind != EXECUTION_EL0_CHILD ||
            (task->execution_state == EXECUTION_READY ||
             task->execution_state == EXECUTION_RUNNING ||
             task->execution_state == EXECUTION_PREEMPTED ||
             task->execution_state == EXECUTION_RETURNING));
}

static void saturating_add(uint64_t* target, uint64_t amount)
{
    if (!target) return;
    *target = amount > UINT64_MAX - *target ? UINT64_MAX : *target + amount;
}

static void* return_context(Task* task)
{
    if (!task || !task->interrupt_context_valid) return 0;
    if (task->execution_kind == EXECUTION_EL0_CHILD &&
        task->execution_state == EXECUTION_PREEMPTED && task->execution_context) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(task->execution_context);
        if ((address & 1) != 0) return 0;
        return reinterpret_cast<void*>(address | 1);
    }
    return task->interrupt_context;
}

static Task* find_next(Task* current, bool include_completion)
{
    if (s_task_slots == 0) return 0;
    uint32_t start = s_round_robin_cursor;
    // Interrupt-return selection can enter a task from its prepared frame
    // without passing through the cooperative cursor update.  Anchor the
    // next walk to the task that is actually running so a blocked task hands
    // off to its immediate runnable successor.
    const uintptr_t currentAddress = reinterpret_cast<uintptr_t>(current);
    const uintptr_t firstTaskAddress = reinterpret_cast<uintptr_t>(&s_tasks[0]);
    const uintptr_t endTaskAddress = reinterpret_cast<uintptr_t>(&s_tasks[s_task_slots]);
    if (currentAddress >= firstTaskAddress && currentAddress < endTaskAddress)
        start = static_cast<uint32_t>(current - &s_tasks[0]);
    for (uint32_t offset = 1; offset <= s_task_slots; ++offset) {
        const uint32_t index = (start + offset) % s_task_slots;
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

    // Keep the common scheduler freestanding even as Config grows; a larger
    // aggregate assignment may lower to an unavailable libc memcpy.
    s_config.allocate_pages = config.allocate_pages;
    s_config.stack_pages = config.stack_pages;
    s_config.cooperative_switch_target = config.cooperative_switch_target;
    s_config.preemption_target = config.preemption_target;
    s_config.release_pages = config.release_pages;
    zero_bytes(s_tasks, sizeof(s_tasks));
    zero_bytes(&s_bootstrap, sizeof(s_bootstrap));
    zero_bytes(&s_stats, sizeof(s_stats));
    s_task_count = 0;
    s_task_slots = 0;
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

    uint32_t slot = 0;
    while (slot < s_task_slots && s_tasks[slot].state != TASK_UNUSED) ++slot;
    if (slot == s_task_slots) {
        if (s_task_slots >= kMaxTasks) return 0;
        ++s_task_slots;
    }
    Task* task = &s_tasks[slot];
    uint64_t stack_base = 0;
    s_config.allocate_pages(s_config.stack_pages, &stack_base);
    if (stack_base == 0) return 0;
    if ((stack_base & UINT64_C(0xfff)) != 0) {
        if (s_config.release_pages) s_config.release_pages(stack_base, s_config.stack_pages);
        return 0;
    }
    uint64_t stack_end = 0;
    if (!gxos_scheduler_add_u64(stack_base, stack_size, &stack_end) ||
        stack_end < UINT64_C(0x10) ||
        !gxos_scheduler_stack_layout_valid(stack_base, stack_size,
                                           stack_end - UINT64_C(0x10))) {
        if (s_config.release_pages) s_config.release_pages(stack_base, s_config.stack_pages);
        return 0;
    }
    for (uint32_t i = 0; i < s_task_slots; ++i) {
        if (s_tasks[i].state == TASK_UNUSED) continue;
        if (stack_ranges_overlap(stack_base, stack_size,
                                 s_tasks[i].stack_base, s_tasks[i].stack_size)) {
            if (s_config.release_pages) s_config.release_pages(stack_base, s_config.stack_pages);
            return 0;
        }
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
        if (s_config.release_pages) s_config.release_pages(stack_base, s_config.stack_pages);
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
    task->el0_preemption_count = 0;
    task->el0_entry_count = 0;
    task->el0_resume_count = 0;
    task->el0_timer_interrupt_count = 0;
    task->wait_count = 0;
    task->wake_count = 0;
    task->execution_id = 0;
    task->owner_id = 0;
    task->execution_budget = 0;
    task->execution_consumed = 0;
    task->execution_started_at = 0;
    task->execution_result = 0;
    task->execution_context = 0;
    task->execution_address_space = 0;
    task->execution_kind = EXECUTION_KERNEL;
    task->execution_state = EXECUTION_NONE;
    task->execution_result_kind = EXECUTION_RESULT_NONE;
    task->cancel_requested = 0;
    task->resources_cleaned = 0;
    return true;
}

bool configure_el0_task(Task* task, uint64_t execution_id, uint64_t owner_id,
                        uint64_t budget, void* execution_context,
                        void* address_space)
{
    if (!is_real_task(task) || task == s_current || task->enabled != 0 ||
        task->state != TASK_BLOCKED || task->execution_kind != EXECUTION_KERNEL ||
        execution_id == 0 || owner_id == 0 || budget == 0 ||
        !execution_context || !address_space || !task_stack_valid(task)) return false;
    task->execution_id = execution_id;
    task->owner_id = owner_id;
    task->execution_budget = budget;
    task->execution_consumed = 0;
    task->execution_started_at = 0;
    task->el0_preemption_count = 0;
    task->el0_entry_count = 0;
    task->el0_resume_count = 0;
    task->el0_timer_interrupt_count = 0;
    task->execution_result = 0;
    task->execution_context = execution_context;
    task->execution_address_space = address_space;
    task->execution_kind = EXECUTION_EL0_CHILD;
    task->execution_state = EXECUTION_CREATED;
    task->execution_result_kind = EXECUTION_RESULT_NONE;
    task->cancel_requested = 0;
    task->resources_cleaned = 0;
    return true;
}

bool start_task(Task* task, uint64_t now)
{
    if (!is_real_task(task) || task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state != EXECUTION_CREATED || task->enabled != 0 ||
        task->state != TASK_BLOCKED || s_phase == PHASE_IDLE || s_failed) return false;
    const kernel::arch::interrupt_state_t irq_state = kernel::arch::irq_save();
    if (!task->context || !task_stack_valid(task) ||
        !kernel::arch::context_to_interrupt(task->context, task->interrupt_context)) {
        kernel::arch::irq_restore(irq_state);
        return false;
    }
    task->interrupt_context_valid = 1;
    task->execution_state = EXECUTION_READY;
    task->execution_started_at = now;
    task->enabled = 1;
    task->state = TASK_RUNNABLE;
    if (s_phase == PHASE_PREEMPTIVE && s_preemptive_complete) {
        s_phase = PHASE_SCHEDULED;
        s_round_robin_cursor = 0;
    }
    kernel::arch::irq_restore(irq_state);
    return true;
}

bool mark_el0_running(Task* task, uint64_t now)
{
    if (!is_real_task(task) || task != s_current ||
        task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state != EXECUTION_READY) return false;
    task->execution_state = EXECUTION_RUNNING;
    task->execution_started_at = now;
    saturating_add(&task->el0_entry_count, 1);
    return true;
}

bool mark_el0_resumed(Task* task, uint64_t now)
{
    if (!is_real_task(task) || task != s_current ||
        task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state != EXECUTION_PREEMPTED) return false;
    task->execution_state = EXECUTION_RUNNING;
    task->execution_started_at = now;
    saturating_add(&task->el0_resume_count, 1);
    return true;
}

bool mark_el0_returning(Task* task, uint64_t now)
{
    if (!is_real_task(task) || task != s_current ||
        task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state != EXECUTION_RUNNING) return false;
    // CNTPCT_EL0 is a 64-bit wrapping counter. Unsigned subtraction preserves
    // a short resident interval across one wrap of the architectural counter.
    saturating_add(&task->execution_consumed, now - task->execution_started_at);
    task->execution_state = EXECUTION_RETURNING;
    return true;
}

bool complete_el0_task(Task* task, ExecutionState state,
                       ExecutionResultKind result_kind, uint64_t result)
{
    if (!is_real_task(task) || task->execution_kind != EXECUTION_EL0_CHILD ||
        (state != EXECUTION_COMPLETED && state != EXECUTION_FAULTED &&
         state != EXECUTION_TIMED_OUT && state != EXECUTION_CANCELLED &&
         state != EXECUTION_START_FAILED) ||
        task->execution_state >= EXECUTION_COMPLETED ||
        result_kind == EXECUTION_RESULT_NONE ||
        (state == EXECUTION_COMPLETED && result_kind != EXECUTION_RESULT_EXIT) ||
        (state == EXECUTION_TIMED_OUT && result_kind != EXECUTION_RESULT_TIMEOUT) ||
        (state == EXECUTION_CANCELLED && result_kind != EXECUTION_RESULT_CANCEL) ||
        (state == EXECUTION_FAULTED && result_kind != EXECUTION_RESULT_FAULT) ||
        (state == EXECUTION_START_FAILED && result_kind != EXECUTION_RESULT_START_FAILURE)) {
        return false;
    }
    task->execution_state = static_cast<uint8_t>(state);
    task->execution_result_kind = static_cast<uint8_t>(result_kind);
    task->execution_result = result;
    task->cancel_requested = 0;
    task->enabled = 0;
    task->state = TASK_FINISHED;
    return true;
}

bool request_el0_cancel(Task* task)
{
    if (!is_real_task(task) || task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state >= EXECUTION_COMPLETED) return false;
    task->cancel_requested = 1;
    if (task != s_current || task->execution_state != EXECUTION_RUNNING) {
        task->execution_state = EXECUTION_CANCELLED;
        task->execution_result_kind = EXECUTION_RESULT_CANCEL;
        task->execution_result = 0;
        task->cancel_requested = 0;
        task->enabled = 0;
        task->state = TASK_FINISHED;
    }
    return true;
}

bool reap_task(Task* task)
{
    if (!is_real_task(task) || task == s_current || task->state != TASK_FINISHED ||
        task->execution_kind != EXECUTION_EL0_CHILD ||
        task->execution_state < EXECUTION_COMPLETED || task->resources_cleaned == 0 ||
        !s_config.release_pages || !task_stack_valid(task)) return false;
    const uint64_t base = task->stack_base;
    const uint64_t pages = task->stack_size / UINT64_C(0x1000);
    if (!s_config.release_pages(base, pages)) return false;
    zero_bytes(task, sizeof(*task));
    task->state = TASK_UNUSED;
    if (s_task_count != 0) --s_task_count;
    return true;
}

bool discard_unstarted_task(Task* task)
{
    if (!is_real_task(task) || task == s_current || task->enabled != 0 ||
        task->state != TASK_BLOCKED || task->execution_kind != EXECUTION_KERNEL ||
        !s_config.release_pages || !task_stack_valid(task)) return false;
    const uint64_t base = task->stack_base;
    const uint64_t pages = task->stack_size / UINT64_C(0x1000);
    if (!s_config.release_pages(base, pages)) return false;
    zero_bytes(task, sizeof(*task));
    task->state = TASK_UNUSED;
    if (s_task_count != 0) --s_task_count;
    return true;
}

void set_phase(Phase phase)
{
    s_phase = phase;
    // Each bounded phase starts a fresh round-robin walk.  Carrying the
    // cooperative cursor into preemption can select a newly enabled service
    // task as the very first exception-return destination instead of entering
    // the normal preemptive worker context.
    s_round_robin_cursor = 0;
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
    for (uint32_t i = 0; i < s_task_slots; ++i) {
        Task* task = &s_tasks[i];
        if (task->state == TASK_UNUSED) continue;
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
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
    phase4_diag_stage(PHASE4_DIAG_SCHED_START_ENTER);
#endif
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
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
    phase4_diag_stage(PHASE4_DIAG_SCHED_START_PRE_IRQ_RESTORE);
#endif
    kernel::arch::irq_restore(state);
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
    phase4_diag_stage(PHASE4_DIAG_SCHED_START_POST_IRQ_RESTORE);
#endif
    kernel::arch::context_switch(&s_bootstrap_context, next->context);
#if defined(GXOS_AARCH64_SCHEDR1_DIAGNOSTICS)
    phase4_diag_stage(PHASE4_DIAG_SCHED_START_RETURN);
#endif
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

bool block_current_locked()
{
    if (!is_real_task(s_current) || !task_is_runnable(s_current) || s_failed) return false;
    Task* old = s_current;
    old->state = TASK_BLOCKED;
    ++old->wait_count;
    ++s_stats.waits;
    /* The caller is running from an interrupt-return context in the
     * preemptive phase.  A cooperative context_switch here would resume the
     * destination's initial/stale stack frame.  Leave the current task in a
     * masked, blocked state and let the timer exception select and restore
     * the next task's interrupt frame.  The blocked task resumes at the WFI
     * below after wake_task_locked marks it runnable. */
    kernel::arch::irq_enable();
    for (;;) {
        if (old->state != TASK_BLOCKED) return !s_failed;
        kernel::arch::idle();
    }
}

void wake_task_locked(Task* task)
{
    if (!is_real_task(task) || task->state != TASK_BLOCKED || task->enabled == 0) return;
    task->state = TASK_RUNNABLE;
    ++task->wake_count;
    ++s_stats.wakes;
}

void wake_task(Task* task)
{
    const kernel::arch::interrupt_state_t state = kernel::arch::irq_save();
    wake_task_locked(task);
    kernel::arch::irq_restore(state);
}

void mark_cooperative_complete()
{
    s_cooperative_complete = true;
}

void* timer_interrupt(void* frame)
{
    return timer_interrupt_at(frame, s_stats.timer_ticks + 1);
}

void* timer_interrupt_at(void* frame, uint64_t monotonic_now)
{
    ++s_stats.timer_ticks;
    if (s_phase == PHASE_IDLE) {
        ++s_stats.idle_wake_ticks;
        return 0;
    }
    if ((s_phase != PHASE_PREEMPTIVE && s_phase != PHASE_SCHEDULED) ||
        !is_real_task(s_current) || (s_phase == PHASE_PREEMPTIVE && s_preemptive_complete)) {
        return 0;
    }

    Task* current = s_current;
    const bool interrupted_el0_running =
        current->execution_kind == EXECUTION_EL0_CHILD &&
        current->execution_state == EXECUTION_RUNNING;
    if (!frame) {
        mark_failure();
        return 0;
    }
    if (current->execution_kind == EXECUTION_EL0_CHILD &&
        current->execution_state == EXECUTION_RUNNING) {
        if (!current->execution_context || !current->execution_address_space) {
            mark_failure();
            return 0;
        }
        saturating_add(&current->el0_timer_interrupt_count, 1);
        saturating_add(&current->execution_consumed,
                       monotonic_now - current->execution_started_at);
        current->execution_state = EXECUTION_PREEMPTED;
        if (current->cancel_requested) {
            if (!complete_el0_task(current, EXECUTION_CANCELLED,
                                   EXECUTION_RESULT_CANCEL, 0)) mark_failure();
        } else if (current->execution_consumed >= current->execution_budget) {
            if (!complete_el0_task(current, EXECUTION_TIMED_OUT,
                                   EXECUTION_RESULT_TIMEOUT, 0)) mark_failure();
        }
    } else {
        if (!task_stack_valid(current) ||
            !kernel::arch::interrupt_frame_save(frame, current->interrupt_context)) {
            mark_failure();
            return 0;
        }
        current->interrupt_context_valid = 1;
    }
    ++current->preemption_count;
    ++s_stats.preemptions;

    if (s_phase == PHASE_PREEMPTIVE &&
        s_stats.preemptions >= s_config.preemption_target && s_completion_task &&
        s_completion_task->enabled && s_completion_task->interrupt_context_valid) {
        s_preemptive_complete = true;
        if (interrupted_el0_running)
            saturating_add(&current->el0_preemption_count, 1);
        count_switch(s_completion_task);
        return return_context(s_completion_task);
    }

    Task* next = find_next(current, false);
    if ((!next || !task_is_runnable(next)) && s_idle_task && task_is_runnable(s_idle_task))
        next = s_idle_task;
    if (!next || !task_is_runnable(next) || !next->interrupt_context_valid) {
        if (!current->enabled && s_completion_task && task_is_runnable(s_completion_task) &&
            s_completion_task->interrupt_context_valid) next = s_completion_task;
        if (!next || !task_is_runnable(next) || !next->interrupt_context_valid) {
            mark_failure();
            return 0;
        }
    }
    if (interrupted_el0_running && next != current)
        saturating_add(&current->el0_preemption_count, 1);
    count_switch(next);
    return return_context(next);
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
        if (s_current->execution_kind == EXECUTION_EL0_CHILD &&
            s_phase == PHASE_SCHEDULED) {
            // A finished EL0 task has no resumable user frame.  Stay on its
            // kernel stack until the next timer exception selects the owner
            // or another runnable task through the normal interrupt path.
            kernel::arch::irq_enable();
            for (;;) kernel::arch::idle();
        }
    }
    return_to_bootstrap();
}

void note_execution()
{
    if (is_real_task(s_current)) ++s_current->execution_count;
}

Task* current_task() { return s_current; }
Task* task_at(uint32_t index)
{
    uint32_t live_index = 0;
    for (uint32_t slot = 0; slot < s_task_slots; ++slot) {
        if (s_tasks[slot].state == TASK_UNUSED) continue;
        if (live_index == index) return &s_tasks[slot];
        ++live_index;
    }
    return 0;
}
uint32_t task_count() { return s_task_count; }
uint64_t cooperative_switches() { return s_stats.cooperative_switches; }
uint64_t preemptions() { return s_stats.preemptions; }
uint64_t idle_wake_ticks() { return s_stats.idle_wake_ticks; }
bool preemptive_complete() { return s_preemptive_complete; }
bool failed() { return s_failed; }

bool stack_integrity()
{
    if (s_failed) return false;
    for (uint32_t i = 0; i < s_task_slots; ++i) {
        if (s_tasks[i].state == TASK_UNUSED) continue;
        if (!task_stack_valid(&s_tasks[i])) return false;
    }
    return true;
}

void note_unexpected_irq(uint32_t irq)
{
    ++s_stats.unexpected_irqs;
    s_stats.last_unexpected_irq = irq;
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
    stats->last_unexpected_irq = s_stats.last_unexpected_irq;
    stats->waits = s_stats.waits;
    stats->wakes = s_stats.wakes;
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
