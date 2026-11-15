#include <stdint.h>

#include "phase2_el0.h"
#include "studio_elf_image.h"
#include "phase2_mmu.h"
#include "phase3_timer.h"
#include "../../../kernel/core/include/kernel/arch_interface.h"
#include "../../../kernel/core/include/kernel/common_scheduler.h"
#include "../../../kernel/core/compiler/compiler_build_service.h"

extern "C" uint64_t phase3_el0_enter(gxos_aarch64_el0_context* context);

namespace {

static const uint32_t kRuntimeCapacity = 16;
static const uint64_t kDefaultBudgetMicroseconds = UINT64_C(500000);
static const uint64_t kMinimumBudgetMicroseconds = UINT64_C(1000);
static const uint64_t kMaximumBudgetMicroseconds = UINT64_C(60000000);
static const uint64_t kReadWriteFlags = GXOS_AARCH64_EL0_PAGE_READ |
                                        GXOS_AARCH64_EL0_PAGE_WRITE;
static const uint64_t kReadExecuteFlags = GXOS_AARCH64_EL0_PAGE_READ |
                                          GXOS_AARCH64_EL0_PAGE_EXECUTE;
static const uint64_t kStackFlags = kReadWriteFlags | GXOS_AARCH64_EL0_PAGE_STACK;
static const uint64_t kRuntimePstate = UINT64_C(0x340);
static const uint64_t kTaskIdBase = UINT64_C(0x80000000);

struct RuntimeRecord {
    gxos_aarch64_el0_address_space space;
    gxos_aarch64_el0_context context;
    kernel::scheduler::Task* task;
    uint64_t execution_id;
    uint64_t parent_owner_id;
    uint64_t user_page_count;
    uint64_t user_table_page_count;
    uint64_t image_entry_point;
    uint64_t final_state;
    uint64_t final_result_kind;
    uint64_t final_result;
    uint8_t in_use;
    uint8_t resources_cleaned;
    uint8_t reserved[6];
};

static RuntimeRecord g_records[kRuntimeCapacity]{};
static uint64_t g_next_execution_id = UINT64_C(0x454c300000000001);
static uint32_t g_next_task_id = 1;

class InterruptGuard {
public:
    InterruptGuard() : saved_(kernel::arch::irq_save()) {}
    ~InterruptGuard() { kernel::arch::irq_restore(saved_); }
private:
    kernel::arch::interrupt_state_t saved_;
};

static const uint8_t g_finite_preemption_payload[] = {
    0x09, 0x7d, 0x80, 0xd2, // mov x9, #1000 (outer count)
    0xea, 0xff, 0x9f, 0xd2, // mov x10, #0xffff (inner count)
    0x4a, 0x05, 0x00, 0xf1, // subs x10, x10, #1
    0xe1, 0xff, 0xff, 0x54, // b.ne -4
    0x29, 0x05, 0x00, 0xf1, // subs x9, x9, #1
    0x81, 0xff, 0xff, 0x54, // b.ne to outer loop (-16)
    0x40, 0x05, 0x80, 0xd2, // mov x0, #42
    0x28, 0x00, 0x80, 0xd2, // mov x8, #EXIT
    0x01, 0x00, 0x00, 0xd4  // svc #0
};
static const uint8_t g_noncooperative_payload[] = {
    0xe9, 0xff, 0x9f, 0xd2, // mov x9, #0xffff
    0x29, 0x09, 0x00, 0xf1, // subs x9, x9, #2 (odd countdown wraps without reaching zero)
    0xe1, 0xff, 0xff, 0x54  // b.ne -4
};
static const uint8_t g_cancellation_payload[] = {
    0x60, 0x08, 0x80, 0xd2, // mov x0, #0x43 (diagnostic only)
    0xff, 0xff, 0xff, 0x17  // b .
};
static const uint8_t g_protected_memory_fault_payload[] = {
    0x09, 0x00, 0xa8, 0xd2, // mov x9, #0x40000000
    0x20, 0x01, 0x40, 0xf9  // ldr x0, [x9]
};

static uint64_t mask_interrupts()
{
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    return 0;
#else
    uint64_t saved_daif = 0;
    __asm__ volatile("mrs %0, daif\n msr daifset, #0xf\n isb"
                     : "=r"(saved_daif) : : "memory");
    return saved_daif;
#endif
}

static void restore_interrupts(uint64_t saved_daif)
{
#if defined(GXOS_AARCH64_EL0_HOST_TEST)
    (void)saved_daif;
#else
    __asm__ volatile("msr daif, %0\n isb" : : "r"(saved_daif) : "memory");
#endif
}

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* bytes = static_cast<uint8_t*>(destination);
    for (uint64_t i = 0; i < size; ++i) bytes[i] = 0;
}

static bool exact_text(const char* left, const char* right)
{
    if (!left || !right) return left == right;
    while (*left && *right) {
        if (*left++ != *right++) return false;
    }
    return *left == '\0' && *right == '\0';
}

static bool owner_is_current_kernel_task(uint64_t owner_id)
{
    kernel::scheduler::Task* current = kernel::scheduler::current_task();
    return current && current->execution_kind == kernel::scheduler::EXECUTION_KERNEL &&
           current->id != 0 && current->id == owner_id;
}

static RuntimeRecord* find_record(uint64_t execution_id, uint64_t owner_id)
{
    if (execution_id == 0 || owner_id == 0) return nullptr;
    for (uint32_t i = 0; i < kRuntimeCapacity; ++i) {
        RuntimeRecord& record = g_records[i];
        if (record.in_use && record.execution_id == execution_id &&
            record.parent_owner_id == owner_id) return &record;
    }
    return nullptr;
}

static RuntimeRecord* allocate_record()
{
    for (uint32_t i = 0; i < kRuntimeCapacity; ++i) {
        if (!g_records[i].in_use ||
            g_records[i].final_state == kernel::scheduler::EXECUTION_REAPED) {
            zero_bytes(&g_records[i], sizeof(g_records[i]));
            return &g_records[i];
        }
    }
    return nullptr;
}

static bool budget_to_counter_ticks(uint64_t budget_microseconds, uint64_t* ticks)
{
    if (!ticks) return false;
    if (budget_microseconds == 0) budget_microseconds = kDefaultBudgetMicroseconds;
    if (budget_microseconds < kMinimumBudgetMicroseconds ||
        budget_microseconds > kMaximumBudgetMicroseconds) return false;
    const uint64_t frequency = phase3_timer_frequency();
    if (frequency == 0) return false;
    const uint64_t whole_seconds = budget_microseconds / UINT64_C(1000000);
    const uint64_t remainder_us = budget_microseconds % UINT64_C(1000000);
    if (whole_seconds != 0 && frequency > UINT64_MAX / whole_seconds) return false;
    const uint64_t whole_ticks = frequency * whole_seconds;
    if (remainder_us != 0 && frequency > UINT64_MAX / remainder_us) return false;
    const uint64_t remainder_product = frequency * remainder_us;
    const uint64_t remainder_ticks =
        remainder_product / UINT64_C(1000000) +
        ((remainder_product % UINT64_C(1000000)) != 0 ? 1 : 0);
    if (remainder_ticks > UINT64_MAX - whole_ticks) return false;
    *ticks = whole_ticks + remainder_ticks;
    return *ticks != 0;
}

// RUNSAFE3-A deliberately accepts a small integer-only instruction profile.
// The kernel is built with general-register-only code generation, and this
// validator prevents a trusted payload from changing SIMD/FPU state or using
// privileged/indirect control-transfer instructions before native state
// ownership is implemented.
static bool trusted_image_valid(const uint8_t* image, uint64_t image_size)
{
    if (!image || image_size == 0 || image_size > GXOS_AARCH64_EL0_PAGE_SIZE ||
        (image_size & 3) != 0) return false;
    const uint64_t instruction_count = image_size / 4;
    bool has_exit = false;
    for (uint64_t index = 0; index < instruction_count; ++index) {
        const uint64_t offset = index * 4;
        const uint32_t word = static_cast<uint32_t>(image[offset]) |
            (static_cast<uint32_t>(image[offset + 1]) << 8) |
            (static_cast<uint32_t>(image[offset + 2]) << 16) |
            (static_cast<uint32_t>(image[offset + 3]) << 24);
        if ((word & UINT32_C(0xff800000)) == UINT32_C(0xd2800000) ||
            (word & UINT32_C(0xffc00000)) == UINT32_C(0xf1000000) ||
            word == UINT32_C(0xf9400120)) {
            continue;
        }
        if ((word & UINT32_C(0xff00001f)) == UINT32_C(0x54000001)) {
            int64_t displacement = static_cast<int64_t>((word >> 5) & UINT32_C(0x7ffff));
            if ((displacement & INT64_C(0x40000)) != 0)
                displacement |= ~INT64_C(0x7ffff);
            const int64_t target = static_cast<int64_t>(index) + displacement;
            if (target < 0 || static_cast<uint64_t>(target) >= instruction_count) return false;
            continue;
        }
        if ((word & UINT32_C(0xfc000000)) == UINT32_C(0x14000000)) {
            int64_t displacement = static_cast<int64_t>(word & UINT32_C(0x03ffffff));
            if ((displacement & INT64_C(0x02000000)) != 0)
                displacement |= ~INT64_C(0x03ffffff);
            const int64_t target = static_cast<int64_t>(index) + displacement;
            if (target < 0 || static_cast<uint64_t>(target) >= instruction_count) return false;
            continue;
        }
        if (word == UINT32_C(0xd4000001)) {
            if (index + 1 != instruction_count || index == 0) return false;
            const uint64_t previous = offset - 4;
            const uint32_t previous_word = static_cast<uint32_t>(image[previous]) |
                (static_cast<uint32_t>(image[previous + 1]) << 8) |
                (static_cast<uint32_t>(image[previous + 2]) << 16) |
                (static_cast<uint32_t>(image[previous + 3]) << 24);
            if (previous_word != UINT32_C(0xd2800028)) return false;
            has_exit = true;
            continue;
        }
        return false;
    }
    (void)has_exit;
    return true;
}

static bool clean_record_resources(RuntimeRecord* record)
{
    if (!record) return false;
    if (record->resources_cleaned) return true;
    if (!phase2_el0_cleanup(&record->space)) return false;
    record->resources_cleaned = 1;
    if (record->task) record->task->resources_cleaned = 1;
    return true;
}

static void scheduled_el0_entry(void* argument);

static void write_le32(uint8_t* destination, uint32_t value)
{
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
    destination[2] = static_cast<uint8_t>(value >> 16);
    destination[3] = static_cast<uint8_t>(value >> 24);
}

static bool make_studio_return_trampoline(uint64_t trampoline_va,
                                          uint64_t gx_main_va,
                                          uint8_t* bytes,
                                          uint64_t capacity)
{
    if (!bytes || capacity < 24 || (trampoline_va & 3) != 0 ||
        (gx_main_va & 3) != 0 || trampoline_va > UINT64_MAX - 4) return false;
    const uint64_t call_pc = trampoline_va + 4;
    int64_t delta = 0;
    if (gx_main_va >= call_pc) {
        const uint64_t forward = gx_main_va - call_pc;
        if (forward > UINT64_C(0x07fffffc)) return false;
        delta = static_cast<int64_t>(forward);
    } else {
        const uint64_t backward = call_pc - gx_main_va;
        if (backward > UINT64_C(0x08000000)) return false;
        delta = -static_cast<int64_t>(backward);
    }
    if ((delta & 3) != 0) return false;
    const uint32_t branch_imm = static_cast<uint32_t>(delta / 4) & UINT32_C(0x03ffffff);
    const uint32_t words[6] = {
        UINT32_C(0xaa1f03e0),             // mov x0, xzr: gx_main receives null ctx
        UINT32_C(0x94000000) | branch_imm, // bl gx_main
        UINT32_C(0x93407c00),              // sxtw x0, w0: preserve signed int result
        UINT32_C(0xd2800028),              // mov x8, #EL0_EXIT
        UINT32_C(0xd4000001),              // svc #0; dispatcher records x0
        UINT32_C(0x14000000)               // fail-closed self-loop if SVC returned
    };
    for (uint32_t i = 0; i < 6; ++i) write_le32(bytes + i * 4, words[i]);
    return true;
}

static bool create_scheduler_record(RuntimeRecord* record, uint64_t owner_id,
                                    uint64_t budget_ticks, uint64_t entry_pc,
                                    uint64_t stack_pointer, uint64_t* execution_id)
{
    if (!record || !execution_id || !record->execution_id || !record->space.identity ||
        !record->space.owner_id || record->space.owner_id != record->execution_id) return false;
    zero_bytes(&record->context, sizeof(record->context));
    record->context.sp_el0 = stack_pointer;
    record->context.entry_pc = entry_pc;
    record->context.pstate = kRuntimePstate;
    record->context.address_space_id = record->space.identity;
    record->context.owner_id = record->space.owner_id;
    record->context.state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
    if (!phase2_el0_validate_context(&record->space, &record->context,
                                    record->space.owner_id)) return false;

    if (g_next_task_id >= UINT32_C(0x7fffffff)) return false;
    const uint32_t task_id = static_cast<uint32_t>(kTaskIdBase | g_next_task_id++);
    record->task = kernel::scheduler::create_task(
        task_id, "studio-el0-child", scheduled_el0_entry, record, false);
    if (!record->task || !kernel::scheduler::configure_el0_task(
            record->task, record->execution_id, owner_id, budget_ticks,
            &record->context, &record->space)) return false;
    record->in_use = 1;
    record->final_state = kernel::scheduler::EXECUTION_CREATED;
    record->user_page_count = record->space.page_count;
    record->user_table_page_count = record->space.table_count;
    *execution_id = record->execution_id;
    return true;
}

static void finish_entry(RuntimeRecord* record, uint64_t state,
                         uint64_t result_kind, uint64_t value)
{
    if (!record || !record->task) return;
    if (!clean_record_resources(record)) {
        // Keep the child task non-runnable while preserving an honest
        // incomplete-cleanup record for the owner to diagnose and retry.
        record->task->enabled = 0;
        record->task->state = kernel::scheduler::TASK_FINISHED;
        return;
    }
    if (!kernel::scheduler::complete_el0_task(
            record->task,
            static_cast<kernel::scheduler::ExecutionState>(state),
            static_cast<kernel::scheduler::ExecutionResultKind>(result_kind), value)) return;
    record->final_state = state;
    record->final_result_kind = result_kind;
    record->final_result = value;
}

static void scheduled_el0_entry(void* argument)
{
    RuntimeRecord* record = static_cast<RuntimeRecord*>(argument);
    kernel::scheduler::Task* task = kernel::scheduler::current_task();
    if (!record || !record->in_use || !record->task || task != record->task ||
        record->context.state != GXOS_AARCH64_EL0_CONTEXT_PREPARED) {
        if (record && record->task) {
            finish_entry(record, kernel::scheduler::EXECUTION_START_FAILED,
                         kernel::scheduler::EXECUTION_RESULT_START_FAILURE, 0);
        }
        kernel::scheduler::thread_exit();
        return;
    }

    const uint64_t saved_daif = mask_interrupts();
    if (!phase2_el0_begin_context(&record->space, &record->context,
                                  record->context.owner_id) ||
        !kernel::scheduler::mark_el0_running(task, phase3_timer_counter())) {
        if (record->space.state == GXOS_AARCH64_EL0_SPACE_ACTIVE)
            phase2_el0_deactivate(&record->space);
        record->context.state = GXOS_AARCH64_EL0_CONTEXT_START_FAILED;
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        finish_entry(record, kernel::scheduler::EXECUTION_START_FAILED,
                     kernel::scheduler::EXECUTION_RESULT_START_FAILURE, 0);
        restore_interrupts(saved_daif);
        kernel::scheduler::thread_exit();
        return;
    }

    const uint64_t returned = phase3_el0_enter(&record->context);
    if (!kernel::scheduler::mark_el0_returning(task, phase3_timer_counter())) {
        record->context.state = GXOS_AARCH64_EL0_CONTEXT_START_FAILED;
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        finish_entry(record, kernel::scheduler::EXECUTION_START_FAILED,
                     kernel::scheduler::EXECUTION_RESULT_START_FAILURE, 0);
        restore_interrupts(saved_daif);
        kernel::scheduler::thread_exit();
        return;
    }
    if (record->context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED &&
        returned == record->context.exit_value) {
        phase2_el0_task_finish_exception(task);
    } else if (record->context.state == GXOS_AARCH64_EL0_CONTEXT_FAULTED) {
        phase2_el0_task_finish_exception(task);
    } else {
        record->context.state = GXOS_AARCH64_EL0_CONTEXT_START_FAILED;
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        finish_entry(record, kernel::scheduler::EXECUTION_START_FAILED,
                     kernel::scheduler::EXECUTION_RESULT_START_FAILURE, 0);
    }
    restore_interrupts(saved_daif);
    kernel::scheduler::thread_exit();
}

} // namespace

extern "C" uint64_t phase2_el0_task_clock_now()
{
    return phase3_timer_counter();
}

extern "C" uint8_t phase2_el0_task_runtime_ready()
{
    volatile uintptr_t api[7];
    api[0] = reinterpret_cast<uintptr_t>(&phase2_el0_task_create);
    api[1] = reinterpret_cast<uintptr_t>(&phase2_el0_task_start);
    api[2] = reinterpret_cast<uintptr_t>(&phase2_el0_task_query);
    api[3] = reinterpret_cast<uintptr_t>(&phase2_el0_task_cancel);
    api[4] = reinterpret_cast<uintptr_t>(&phase2_el0_task_reap);
    api[5] = reinterpret_cast<uintptr_t>(&phase2_el0_task_finish_preempted);
    api[6] = reinterpret_cast<uintptr_t>(&phase2_el0_task_finish_exception);
    // The kernel calls this after scheduler initialization but before its
    // first task is created. An empty task table is a valid ready state here.
    return phase3_timer_frequency() != 0 &&
           api[0] != 0 && api[1] != 0 && api[2] != 0 && api[3] != 0 &&
           api[4] != 0 && api[5] != 0 && api[6] != 0;
}

extern "C" uint8_t phase2_el0_task_create(
    const uint8_t* trusted_image, uint64_t image_size, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id)
{
    if (execution_id) *execution_id = 0;
    if (!execution_id || !trusted_image || !trusted_image_valid(trusted_image, image_size)) return 0;
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id) || g_next_execution_id == 0 ||
        g_next_execution_id == UINT64_MAX || g_next_task_id >= UINT32_C(0x7fffffff)) return 0;
    uint64_t budget_ticks = 0;
    if (!budget_to_counter_ticks(budget_microseconds, &budget_ticks)) return 0;
    RuntimeRecord* record = allocate_record();
    if (!record) return 0;

    record->execution_id = g_next_execution_id++;
    record->parent_owner_id = owner_id;
    const uint64_t child_owner = record->execution_id;
    if (!phase2_el0_address_space_create(&record->space, child_owner)) {
        zero_bytes(record, sizeof(*record));
        return 0;
    }
    const uint64_t code_va = GXOS_AARCH64_EL0_USER_BASE;
    const uint64_t stack_va = GXOS_AARCH64_EL0_USER_BASE + UINT64_C(0x4000);
    uint64_t code_physical = 0;
    uint64_t stack_physical = 0;
    if (!phase2_el0_allocate_page(&record->space, child_owner, code_va,
                                  kReadExecuteFlags, &code_physical) ||
        !phase2_el0_allocate_page(&record->space, child_owner, stack_va,
                                  kStackFlags, &stack_physical) ||
        !phase2_el0_copy_code(&record->space, child_owner, code_va,
                              trusted_image, image_size)) {
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        phase2_el0_cleanup(&record->space);
        zero_bytes(record, sizeof(*record));
        return 0;
    }
    record->user_page_count = record->space.page_count;
    record->user_table_page_count = record->space.table_count;
    zero_bytes(&record->context, sizeof(record->context));
    record->context.sp_el0 = stack_va + GXOS_AARCH64_EL0_PAGE_SIZE;
    record->context.entry_pc = code_va;
    record->context.pstate = kRuntimePstate;
    record->context.address_space_id = record->space.identity;
    record->context.owner_id = child_owner;
    record->context.state = GXOS_AARCH64_EL0_CONTEXT_PREPARED;
    if (!phase2_el0_validate_context(&record->space, &record->context, child_owner)) {
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        phase2_el0_cleanup(&record->space);
        zero_bytes(record, sizeof(*record));
        return 0;
    }

    const uint32_t task_id = static_cast<uint32_t>(kTaskIdBase | g_next_task_id++);
    record->task = kernel::scheduler::create_task(
        task_id, "el0-child", scheduled_el0_entry, record, false);
    if (!record->task || !kernel::scheduler::configure_el0_task(
            record->task, record->execution_id, owner_id, budget_ticks,
            &record->context, &record->space)) {
        if (record->task) kernel::scheduler::discard_unstarted_task(record->task);
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        phase2_el0_cleanup(&record->space);
        zero_bytes(record, sizeof(*record));
        return 0;
    }
    record->in_use = 1;
    record->final_state = kernel::scheduler::EXECUTION_CREATED;
    *execution_id = record->execution_id;
    return 1;
}

extern "C" uint8_t phase2_el0_task_create_studio_elf(
    const uint8_t* validated_elf, uint64_t elf_size, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id)
{
    if (execution_id) *execution_id = 0;
    gxos_aarch64_studio_elf_image image{};
    if (!execution_id || !validated_elf ||
        !gxos_aarch64_studio_elf_validate(validated_elf, elf_size, &image)) return 0;
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id) || g_next_execution_id == 0 ||
        g_next_execution_id == UINT64_MAX || g_next_task_id >= UINT32_C(0x7fffffff)) return 0;
    uint64_t budget_ticks = 0;
    if (!budget_to_counter_ticks(budget_microseconds, &budget_ticks)) return 0;
    RuntimeRecord* record = allocate_record();
    if (!record) return 0;

    record->execution_id = g_next_execution_id++;
    record->parent_owner_id = owner_id;
    record->image_entry_point = image.entry_point;
    const uint64_t child_owner = record->execution_id;
    if (!phase2_el0_address_space_create(&record->space, child_owner)) {
        zero_bytes(record, sizeof(*record));
        return 0;
    }

    const uint64_t rx_flags = kReadExecuteFlags;
    const uint64_t stack_flags = kStackFlags;
    bool prepared = true;
    for (uint64_t page = 0; page < image.image_page_count; ++page) {
        uint64_t physical = 0;
        if (!phase2_el0_allocate_page(
                &record->space, child_owner,
                image.image_base + page * GXOS_AARCH64_EL0_PAGE_SIZE,
                rx_flags, &physical)) {
            prepared = false;
            break;
        }
    }
    if (prepared) {
        prepared = phase2_el0_copy_owned_memory(
            &record->space, child_owner, image.image_base,
            validated_elf + image.segment_file_offset,
            image.segment_file_bytes) != 0;
    }

    uint64_t trampoline_physical = 0;
    uint8_t trampoline[24] = {};
    if (prepared) {
        prepared = phase2_el0_allocate_page(
            &record->space, child_owner,
            GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA,
            rx_flags, &trampoline_physical) != 0 &&
            make_studio_return_trampoline(
                GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA,
                image.entry_point, trampoline, sizeof(trampoline)) &&
            phase2_el0_copy_owned_memory(
                &record->space, child_owner,
                GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA,
                trampoline, sizeof(trampoline)) != 0;
    }

    const uint64_t stack_end = GXOS_AARCH64_EL0_USER_LIMIT - GXOS_AARCH64_EL0_PAGE_SIZE;
    const uint64_t stack_base = stack_end - GXOS_AARCH64_STUDIO_ELF_STACK_BYTES;
    for (uint64_t offset = 0; prepared &&
         offset < GXOS_AARCH64_STUDIO_ELF_STACK_BYTES;
         offset += GXOS_AARCH64_EL0_PAGE_SIZE) {
        uint64_t physical = 0;
        prepared = phase2_el0_allocate_page(
            &record->space, child_owner, stack_base + offset,
            stack_flags, &physical) != 0;
    }

    if (!prepared || !create_scheduler_record(
            record, owner_id, budget_ticks,
            GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA,
            stack_end, execution_id)) {
        if (record->task) {
            kernel::scheduler::discard_unstarted_task(record->task);
            record->task = nullptr;
        }
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        (void)phase2_el0_cleanup(&record->space);
        zero_bytes(record, sizeof(*record));
        *execution_id = 0;
        return 0;
    }
    return 1;
}

extern "C" uint8_t phase2_el0_task_create_studio_build(
    const gxos_aarch64_studio_build_identity* identity, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id)
{
    if (execution_id) *execution_id = 0;
    if (!execution_id || !identity || !identity->source_sha256) return 0;
    InterruptGuard guard;
    kernel::compiler::BareMetalBuildService::OwnedArtifactView artifact{};
    if (!kernel::compiler::BareMetalBuildService::current_artifact(
            identity->owner_identity, identity->build_generation,
            identity->project_id, identity->artifact_path, identity->artifact_size,
            identity->architecture, identity->abi, identity->package_path,
            identity->package_generation, identity->artifact_sha256,
            &artifact) ||
        !artifact.el0IntegerProfileValid || !artifact.bytes ||
        !exact_text(identity->source_sha256, artifact.sourceSha256)) return 0;

    // Copy from the immutable kernel-owned Build snapshot into child-owned RX
    // pages while the Build identity is still current. The child never reopens
    // the VFS artifact path after this point.
    return phase2_el0_task_create_studio_elf(
        artifact.bytes, artifact.artifactSize, owner_id,
        budget_microseconds, execution_id);
}

extern "C" uint8_t phase2_el0_task_start(uint64_t execution_id, uint64_t owner_id)
{
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id)) return 0;
    RuntimeRecord* record = find_record(execution_id, owner_id);
    if (!record || !record->task ||
        record->final_state != kernel::scheduler::EXECUTION_CREATED) return 0;
    if (!kernel::scheduler::start_task(record->task, phase3_timer_counter())) {
        return 0;
    }
    kernel::scheduler::set_phase(kernel::scheduler::PHASE_SCHEDULED);
    record->final_state = kernel::scheduler::EXECUTION_READY;
    phase3_timer_start();
    return 1;
}

extern "C" uint8_t phase2_el0_task_query(
    uint64_t execution_id, uint64_t owner_id,
    gxos_aarch64_el0_task_result* result)
{
    if (!result) return 0;
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id)) return 0;
    RuntimeRecord* record = find_record(execution_id, owner_id);
    if (!record) return 0;
    result->execution_id = record->execution_id;
    result->owner_id = record->parent_owner_id;
    result->compiled_entry_point = record->image_entry_point;
    result->el0_initial_pc = record->context.entry_pc;
    result->image_entry_descriptor = 0;
    result->trampoline_descriptor = 0;
    result->user_stack_descriptor = 0;
    result->lower_stack_guard_descriptor = 0;
    result->upper_stack_guard_descriptor = 0;
    if (record->space.root != 0) {
        const uint64_t stack_end = GXOS_AARCH64_EL0_USER_LIMIT - GXOS_AARCH64_EL0_PAGE_SIZE;
        const uint64_t stack_base = stack_end - GXOS_AARCH64_STUDIO_ELF_STACK_BYTES;
        (void)phase2_el0_query_descriptor(&record->space, record->image_entry_point,
                                          &result->image_entry_descriptor);
        (void)phase2_el0_query_descriptor(&record->space,
            GXOS_AARCH64_STUDIO_ELF_TRAMPOLINE_VA, &result->trampoline_descriptor);
        (void)phase2_el0_query_descriptor(&record->space, stack_base,
                                          &result->user_stack_descriptor);
        (void)phase2_el0_query_descriptor(&record->space,
            stack_base - GXOS_AARCH64_EL0_PAGE_SIZE,
            &result->lower_stack_guard_descriptor);
        (void)phase2_el0_query_descriptor(&record->space, stack_end,
                                          &result->upper_stack_guard_descriptor);
    }
    if (record->task) {
        result->state = record->task->execution_state;
        result->result_kind = record->task->execution_result_kind;
        result->result_value = record->task->execution_result;
        result->scheduled_counter_ticks = record->task->execution_consumed;
        result->budget_counter_ticks = record->task->execution_budget;
        result->address_space_id = record->space.identity;
        result->address_space_owner_id = record->space.owner_id;
        result->scheduler_task_id = record->task->id;
        result->kernel_stack_base = record->task->stack_base;
        result->kernel_stack_size = record->task->stack_size;
        result->user_stack_pointer = record->context.sp_el0;
        result->user_page_count = record->user_page_count;
        result->page_table_page_count = record->user_table_page_count;
        result->preemption_count = record->task->el0_preemption_count;
        result->el0_entry_count = record->task->el0_entry_count;
        result->el0_resume_count = record->task->el0_resume_count;
        result->el0_timer_interrupt_count = record->task->el0_timer_interrupt_count;
        result->saved_x0 = record->context.x[0];
        result->saved_x9 = record->context.x[9];
        result->fault_esr = record->context.fault_esr;
        result->fault_elr = record->context.fault_elr;
        result->fault_spsr = record->context.fault_spsr;
        result->fault_far = record->context.fault_far;
        result->kernel_root_restored =
            phase2_mmu_current_root() == record->space.kernel_root;
        result->cancel_requested = record->task->cancel_requested;
        result->resources_cleaned = record->task->resources_cleaned;
    } else {
        result->state = record->final_state;
        result->result_kind = record->final_result_kind;
        result->result_value = record->final_result;
        result->scheduled_counter_ticks = 0;
        result->budget_counter_ticks = 0;
        result->address_space_id = record->space.identity;
        result->address_space_owner_id = record->space.owner_id;
        result->scheduler_task_id = 0;
        result->kernel_stack_base = 0;
        result->kernel_stack_size = 0;
        result->user_stack_pointer = 0;
        result->user_page_count = record->user_page_count;
        result->page_table_page_count = record->user_table_page_count;
        result->preemption_count = 0;
        result->el0_entry_count = 0;
        result->el0_resume_count = 0;
        result->el0_timer_interrupt_count = 0;
        result->saved_x0 = record->context.x[0];
        result->saved_x9 = record->context.x[9];
        result->fault_esr = record->context.fault_esr;
        result->fault_elr = record->context.fault_elr;
        result->fault_spsr = record->context.fault_spsr;
        result->fault_far = record->context.fault_far;
        result->kernel_root_restored =
            phase2_mmu_current_root() == record->space.kernel_root;
        result->cancel_requested = 0;
        result->resources_cleaned = record->resources_cleaned;
    }
    return 1;
}

extern "C" uint8_t phase2_el0_task_cancel(uint64_t execution_id, uint64_t owner_id)
{
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id)) return 0;
    RuntimeRecord* record = find_record(execution_id, owner_id);
    if (!record || !record->task) return 0;
    const bool accepted = kernel::scheduler::request_el0_cancel(record->task);
    const bool immediate = accepted &&
        record->task->execution_state == kernel::scheduler::EXECUTION_CANCELLED;
    if (immediate) {
        if (!phase2_el0_terminate_preempted(
                &record->space, &record->context, record->context.owner_id,
                GXOS_AARCH64_EL0_CONTEXT_CANCELLED)) {
            return 0;
        }
        record->resources_cleaned = 1;
        record->task->resources_cleaned = 1;
        record->final_state = kernel::scheduler::EXECUTION_CANCELLED;
        record->final_result_kind = kernel::scheduler::EXECUTION_RESULT_CANCEL;
        record->final_result = 0;
    }
    return accepted ? 1 : 0;
}

extern "C" uint8_t phase2_el0_task_reap(uint64_t execution_id, uint64_t owner_id)
{
    InterruptGuard guard;
    if (!owner_is_current_kernel_task(owner_id)) return 0;
    RuntimeRecord* record = find_record(execution_id, owner_id);
    if (!record || !record->task || !record->resources_cleaned) return 0;
    if (record->task->execution_state < kernel::scheduler::EXECUTION_COMPLETED ||
        !kernel::scheduler::reap_task(record->task)) return 0;
    record->task = nullptr;
    record->final_state = kernel::scheduler::EXECUTION_REAPED;
    return 1;
}

extern "C" uint8_t phase2_el0_task_finish_preempted(void* scheduler_task)
{
    kernel::scheduler::Task* task =
        static_cast<kernel::scheduler::Task*>(scheduler_task);
    if (!task || task->execution_kind != kernel::scheduler::EXECUTION_EL0_CHILD ||
        (task->execution_state != kernel::scheduler::EXECUTION_TIMED_OUT &&
         task->execution_state != kernel::scheduler::EXECUTION_CANCELLED) ||
        !task->argument) return 0;
    RuntimeRecord* record = static_cast<RuntimeRecord*>(task->argument);
    if (!record->in_use || record->task != task ||
        !phase2_el0_terminate_preempted(
            &record->space, &record->context, record->context.owner_id,
            task->execution_state == kernel::scheduler::EXECUTION_TIMED_OUT
                ? GXOS_AARCH64_EL0_CONTEXT_TIMED_OUT
                : GXOS_AARCH64_EL0_CONTEXT_CANCELLED)) return 0;
    record->resources_cleaned = 1;
    record->final_state = task->execution_state;
    record->final_result_kind = task->execution_result_kind;
    record->final_result = task->execution_result;
    task->resources_cleaned = 1;
    return 1;
}

extern "C" uint8_t phase2_el0_task_finish_exception(void* scheduler_task)
{
    kernel::scheduler::Task* task =
        static_cast<kernel::scheduler::Task*>(scheduler_task);
    if (!task || task->execution_kind != kernel::scheduler::EXECUTION_EL0_CHILD ||
        !task->argument) return 0;
    RuntimeRecord* record = static_cast<RuntimeRecord*>(task->argument);
    if (!record->in_use || record->task != task ||
        task->execution_state != kernel::scheduler::EXECUTION_RETURNING ||
        record->space.state != GXOS_AARCH64_EL0_SPACE_TERMINAL ||
        record->space.root == phase2_mmu_current_root()) return 0;
    if (task->cancel_requested) {
        record->context.state = GXOS_AARCH64_EL0_CONTEXT_CANCELLED;
        if (!kernel::scheduler::complete_el0_task(
                task, kernel::scheduler::EXECUTION_CANCELLED,
                kernel::scheduler::EXECUTION_RESULT_CANCEL, 0)) return 0;
    } else if (task->execution_consumed >= task->execution_budget) {
        record->context.state = GXOS_AARCH64_EL0_CONTEXT_TIMED_OUT;
        if (!kernel::scheduler::complete_el0_task(
                task, kernel::scheduler::EXECUTION_TIMED_OUT,
                kernel::scheduler::EXECUTION_RESULT_TIMEOUT, 0)) return 0;
    }
    if (task->execution_state == kernel::scheduler::EXECUTION_CANCELLED ||
        task->execution_state == kernel::scheduler::EXECUTION_TIMED_OUT) {
        if (record->context.state != GXOS_AARCH64_EL0_CONTEXT_CANCELLED &&
            record->context.state != GXOS_AARCH64_EL0_CONTEXT_TIMED_OUT) return 0;
        record->space.state = GXOS_AARCH64_EL0_SPACE_TERMINAL;
        if (!clean_record_resources(record)) return 0;
        record->final_state = task->execution_state;
        record->final_result_kind = task->execution_result_kind;
        record->final_result = 0;
        return true;
    }
    if (record->context.state == GXOS_AARCH64_EL0_CONTEXT_EXITED) {
        finish_entry(record, kernel::scheduler::EXECUTION_COMPLETED,
                     kernel::scheduler::EXECUTION_RESULT_EXIT,
                     record->context.exit_value);
    } else if (record->context.state == GXOS_AARCH64_EL0_CONTEXT_FAULTED) {
        finish_entry(record, kernel::scheduler::EXECUTION_FAULTED,
                     kernel::scheduler::EXECUTION_RESULT_FAULT,
                     record->context.fault_reason);
    } else {
        return 0;
    }
    return record->resources_cleaned && task->execution_state >=
           kernel::scheduler::EXECUTION_COMPLETED;
}

extern "C" uint8_t phase2_el0_task_create_fixture(
    uint64_t fixture_kind, uint64_t owner_id,
    uint64_t budget_microseconds, uint64_t* execution_id)
{
    const uint8_t* payload = nullptr;
    uint64_t payload_size = 0;
    switch (fixture_kind) {
    case GXOS_AARCH64_EL0_FIXTURE_FINITE_PREEMPTION:
        payload = g_finite_preemption_payload;
        payload_size = sizeof(g_finite_preemption_payload);
        break;
    case GXOS_AARCH64_EL0_FIXTURE_NONCOOPERATIVE:
        payload = g_noncooperative_payload;
        payload_size = sizeof(g_noncooperative_payload);
        break;
    case GXOS_AARCH64_EL0_FIXTURE_CANCELLATION:
        payload = g_cancellation_payload;
        payload_size = sizeof(g_cancellation_payload);
        break;
    case GXOS_AARCH64_EL0_FIXTURE_PROTECTED_MEMORY_FAULT:
        payload = g_protected_memory_fault_payload;
        payload_size = sizeof(g_protected_memory_fault_payload);
        break;
    default:
        return 0;
    }
    return phase2_el0_task_create(payload, payload_size, owner_id,
                                  budget_microseconds, execution_id);
}
